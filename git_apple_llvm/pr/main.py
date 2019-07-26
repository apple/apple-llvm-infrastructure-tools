#!/usr/bin/env python3
"""
  Script that implements `git apple-llvm pr`.
"""

from git_apple_llvm.pr.pr_tool import PRTool, PullRequest, PullRequestState
from git_apple_llvm.pr.github_pr_tool import create_github_pr_tool
from git_apple_llvm.git_tools import git_output, get_current_checkout_directory
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


class PRToolType(Enum):
    GitHub = 1


def pr_tool_type_from_string(s: str) -> Optional[PRToolType]:
    if s == 'github':
        return PRToolType.GitHub
    return None


class PRToolConfiguration:
    """
    Contains the information about which repository is operated on
    for pull requests.

    Attributes:
    type    The type of git hosting service.
    domain  The domain of the git hosting service (e.g. github.com)
    user    Which username owns the repo (e.g. apple)
    repo    The name of the actual repository (e.g. apple-llvm-infrastructure-tools)
    """

    def __init__(self, type: PRToolType, domain: str, user: str, repo: str):
        self.type = type
        self.domain = domain
        self.user = user
        self.repo = repo

    def create_tool(self) -> PRTool:
        if self.type == PRToolType.GitHub:
            return self.create_github_tool()
        raise ValueError('invalid tool type')

    def create_github_tool(self) -> PRTool:
        assert self.type == PRToolType.GitHub
        return create_github_pr_tool(self.domain, self.user, self.repo)


def load_pr_config() -> Optional[PRToolConfiguration]:
    config = git_output(
        'show', f'HEAD:apple-llvm-config/pr.json', ignore_error=True)
    if not config:
        return None
    value = json.loads(config)
    type = pr_tool_type_from_string(value['type'])
    if not type:
        return None
    # FIXME: Validate json.
    return PRToolConfiguration(type=type,
                               domain=value['domain'],
                               user=value['user'],
                               repo=value['repo'])


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

    global pr_tool
    if pr_tool:
        return
    pr_tool = config.create_tool()


@pr.command()
@click.option('--target', type=str,
              help='List pull requests for the specified target branch only')
def list(target):
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
def test(pr_ref: PullRequestRef):
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
    pr.test()
    click.echo('✅ you commented "@swift-ci please test" on the pull request.')


if __name__ == '__main__':
    pr()
