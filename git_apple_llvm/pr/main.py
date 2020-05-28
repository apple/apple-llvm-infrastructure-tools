#!/usr/bin/env python3
"""
  Script that implements `git apple-llvm pr`.
"""

from git_apple_llvm.pr.pr_tool import PRTool, PullRequest, PullRequestState
from git_apple_llvm.pr.github_pr_tool import create_github_pr_tool
from git_apple_llvm.ci import CISystemType
from git_apple_llvm.git_tools import git_output, get_current_checkout_directory
from git_apple_llvm.git_tools.tracked_branch_ref import TrackedBranchRef, get_tracked_branch_ref
import click
import logging
from enum import Enum
from typing import Optional
import sys
import json
from functools import partial
import abc
import textwrap

log = logging.getLogger(__name__)
pr_tool: PRTool = None
ci_test_type: Optional[CISystemType] = None


class PRToolType(Enum):
    GitHub = 1


def pr_tool_type_from_string(s: str) -> Optional[PRToolType]:
    if s == 'github':
        return PRToolType.GitHub
    return None


def pr_test_type_from_string(s: str) -> Optional[CISystemType]:
    if s == 'swift-ci':
        return CISystemType.SwiftCI
    if s == 'jenkins-test-plans':
        return CISystemType.JenkinsTestPlans
    return None


class PRToolConfiguration:
    """
    Contains the information about which repository is operated on
    for pull requests.

    Attributes:
    type      The type of git hosting service.
    domain    The domain of the git hosting service (e.g. github.com)
    user      Which username owns the repo (e.g. apple)
    repo      The name of the actual repository (e.g. apple-llvm-infrastructure-tools)
    test_type The type of testing service to use for PRs (e.g. swift-ci).
    """

    def __init__(self, type: PRToolType, domain: str, user: str, repo: str, test_type: Optional[CISystemType]):
        self.type = type
        self.domain = domain
        self.user = user
        self.repo = repo
        self.test_type = test_type

    def create_tool(self) -> PRTool:
        if self.type == PRToolType.GitHub:
            return self.create_github_tool()
        raise ValueError('invalid tool type')

    def create_github_tool(self) -> PRTool:
        assert self.type == PRToolType.GitHub
        return create_github_pr_tool(self.domain, self.user, self.repo)


def load_pr_config() -> Optional[PRToolConfiguration]:
    config = git_output(
        'show', 'HEAD:apple-llvm-config/pr.json', ignore_error=True)
    if not config:
        return None
    value = json.loads(config)
    type = pr_tool_type_from_string(value['type'])
    if not type:
        return None
    # FIXME: Validate json.
    test_type: Optional[CISystemType] = None
    if 'test' in value:
        test_type = pr_test_type_from_string(value['test']['type'])
    return PRToolConfiguration(type=type,
                               domain=value['domain'],
                               user=value['user'],
                               repo=value['repo'],
                               test_type=test_type)


def fatal(message: str):
    click.echo(click.style('fatal: ', fg='red') + message, err=True)
    sys.exit(1)


@click.group()
@click.option('--verbose/--no-verbose', default=False)
def pr(verbose):
    """ Tool for working with pull requests """
    logging.basicConfig(level=logging.DEBUG if verbose else logging.WARNING,
                        format='%(levelname)s: %(message)s [%(filename)s:%(lineno)d at %(asctime)s] ',)

    # Verify that we're in a git checkout.
    if not get_current_checkout_directory():
        fatal('not a git repository')

    # Load the config file.
    config = load_pr_config()
    if not config:
        fatal('missing `git apple-llvm pr` configuration file')

    global pr_tool, ci_test_type
    ci_test_type = config.test_type
    if pr_tool:
        return
    pr_tool = config.create_tool()


@pr.command()
@click.option('--target', type=str,
              help='List pull requests for the specified target branch only')
def list(target):
    """ List pull requests for this repository """
    def _ident(x):
        return x
    filter_func = _ident
    if target:
        filter_func = partial(filter, lambda x: x.base_branch == target)
    for pr in filter_func(pr_tool.list()):
        # Print the title and the URL.
        number_text = f'- [#{pr.number}] '
        wrapper = textwrap.TextWrapper()
        wrapper.initial_indent = number_text
        wrapper.subsequent_indent = '  '
        wrapper.width = 80
        title = wrapper.fill(pr.title)
        click.echo(click.style(f'{title}', bold=True))
        click.echo(f'  {pr.url}')
        click.echo('')
        body_text = pr.body_text
        if not len(body_text):
            continue
        # Print the body out.
        wrapper.initial_indent = wrapper.subsequent_indent
        body_lines = wrapper.wrap(body_text)
        if len(body_lines) > 5:
            body_lines = body_lines[:4] + ['  ...']
        body = '\n'.join(body_lines)
        click.echo(f'{body}')
        click.echo('')


class PullRequestRef(abc.ABC):
    """ An abstract class that represents a PR reference (pr-id or branch) """


class PullRequestNumber(PullRequestRef):
    """ A class that represents a pull request referenced by its number (#X) """

    def __init__(self, pr_number: int):
        self.pr_number = pr_number

    def __repr__(self) -> str:
        return f'#{self.pr_number}'


class PullRequestParamType(click.ParamType):
    name = '<#pr / branch-name>'

    def convert(self, value, param, ctx):
        if value.startswith('#'):
            try:
                return PullRequestNumber(int(value[1:]))
            except ValueError:
                self.fail(f'{value!r} is not a valid integer', param, ctx)
        self.fail(
            f'{value!r} is not a valid pull request number of a branch name', param, ctx)


def make_pr_number_ref(pr: PullRequestRef) -> PullRequestNumber:
    assert isinstance(pr, PullRequestNumber)
    return pr


def max_length(text: str, max_len: int) -> str:
    assert max_len > 3
    if len(text) <= max_len:
        return text
    return text[:max_len - 3] + '...'


def shorten(text: str) -> str:
    return max_length(text, max_len=40)


@pr.command()
@click.argument('pr_ref', metavar='<#pr / branch-name>',
                type=PullRequestParamType(), required=True)
@click.option('-t', '--test', metavar='<test_plan>', type=str,
              default='pr',
              help='The test plan which should run (default: pr)')
def test(pr_ref: PullRequestRef, test: str):
    """ Run tests for a particular pull request """
    pr_number = make_pr_number_ref(pr_ref).pr_number
    pr: Optional[PullRequest] = pr_tool.get_pr_from_number(pr_number)
    if not pr:
        fatal(f'pull request #{pr_number} does not exist')
    assert pr  # Type checking appeasement.
    if pr.info.state != PullRequestState.Open:
        fatal(
            f'pull request #{pr_number} ({shorten(pr.info.title)}) is no longer open')

    click.echo(click.style(
        f'Triggering pull request testing for pr #{pr_number} by {pr.info.author_username}:', bold=True))
    click.echo(f'  {max_length(pr.info.title, 78)}')
    global ci_test_type
    if ci_test_type == CISystemType.SwiftCI:
        if test != 'pr':
            fatal(f'swift-ci does not support test plan "{test}"')
        pr.test_swift_ci()
        click.echo('✅ you commented "@swift-ci please test" on the pull request.')
    elif ci_test_type == CISystemType.JenkinsTestPlans:
        pr.test_jenkins_test_plans(test)
    else:
        fatal('this repository does not support PR testing')


@pr.command()
@click.option('-m', '--title', type=str,
              help='Pull request title', required=True)
@click.option('-h', '--head', metavar='<branch>', type=str, required=True,
              help='Pull request branch containing the changes (default: HEAD)')
@click.option('-b', '--base', metavar='<branch>', type=str, required=True,
              help='Base branch into which the pull request will be merged')
@click.option('--dry-run', is_flag=True, default=False,
              help='Dry run; do not create the pull request')
def create(title: str, head: str, base: str, dry_run: bool):
    """ Create a new pull request """
    hb: Optional[TrackedBranchRef] = get_tracked_branch_ref(head)
    if not hb:
        fatal(f'head branch "{head}" is not a valid remote tracking branch')
        return
    log.info('HEAD branch: %s from %s', hb.branch_name, hb.remote_url)
    bb: Optional[TrackedBranchRef] = get_tracked_branch_ref(base)
    if not bb:
        fatal(f'base branch "{base}" is not a valid remote tracking branch')
        return
    log.info('BASE branch: %s from %s', bb.branch_name, bb.remote_url)

    click.echo('Creating pull request:')
    remote_prefix = ''
    if hb.remote_url != bb.remote_url:
        remote_prefix = f'{hb.remote_name}:'
    else:
        remote_prefix = ''
    click.echo(
        f'  {remote_prefix}{hb.branch_name} -> {bb.branch_name} on {bb.remote_url}')

    # FIXME: The base might be old by this point, and we might not have it!
    # click.echo('\nWith the following changes:')
    # git('log', '--format=%h %s', '--graph', hb.head_hash,
    #    '--not', bb.head_hash)
    # click.echo('')
    if dry_run:
        click.echo('🛑 dry run, stopping before creating the pull request.')
        return
    pr = pr_tool.create_pr(title, base_branch=bb.branch_name,
                           head_repo_url=hb.remote_url, head_branch=hb.branch_name)
    click.echo(f'✅ Created a pull request #{pr.info.number} ({pr.info.url})')


if __name__ == '__main__':
    pr()
