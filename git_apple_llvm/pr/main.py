#!/usr/bin/env python3
"""
  Script that implements `git apple-llvm pr`.
"""

from git_apple_llvm.pr.pr_tool import PRTool
from git_apple_llvm.pr.github_pr_tool import create_github_pr_tool
from git_apple_llvm.git_tools import git_output, get_current_checkout_directory
import click
import logging
from enum import Enum
from typing import Optional
import sys
import json

log = logging.getLogger(__name__)
pr_tool = None


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
def list():
    for pr in pr_tool.list():
        print(f'[#{pr.number}] {pr.title}')
        print('')
        print(f'{pr.body_text}')
        print(f'{pr.url}')
        print('')


if __name__ == '__main__':
    pr()
