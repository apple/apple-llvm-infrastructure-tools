"""
  Module for computing the automerger status.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import find_inflight_merges, compute_unmerged_commits, has_merge_conflict
from git_apple_llvm.am.oracle import get_ci_status
from git_apple_llvm.am.zippered_merge import compute_zippered_merges
import logging
import click
from typing import Dict, List, Set, Optional


log = logging.getLogger(__name__)


def compute_inflight_commit_count(commits: List[str], commits_inflight: Set[str]) -> int:
    # Scan until we find the first in-flight commit.
    not_seen = 0
    for commit in commits:
        if commit.split(' ')[0] in commits_inflight:
            return len(commits) - not_seen
        not_seen += 1
    return 0


def print_edge_status(upstream_branch: str, target_branch: str,
                      inflight_merges: Dict[str, List[str]], list_commits: bool = False, remote: str = 'origin',
                      query_ci_status: bool = False):
    commits_inflight: Set[str] = set(
        inflight_merges[target_branch] if target_branch in inflight_merges else [])

    click.echo(click.style(
               f'[{upstream_branch} -> {target_branch}]', bold=True))
    commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=target_branch,
                                                            upstream_branch=upstream_branch, format='%H %cd')
    if not commits:
        print(f'- There are no unmerged commits. The {target_branch} branch is up to date.')
        return
    inflight_count = compute_inflight_commit_count(commits, commits_inflight)
    str2 = f'{inflight_count} commits are currently being merged/build/tested.'
    print(f'- There are {len(commits)} unmerged commits. {str2}')
    print('- Unmerged commits:')

    def print_commit_status(commit: str, check_for_merge_conflict: bool = False):
        hash = commit.split(' ')[0]
        status: str = ''
        if query_ci_status:
            ci_state: Optional[str] = get_ci_status(hash, target_branch)
            if ci_state:
                status = ci_state
        if not status:
            if hash in commits_inflight:
                status = 'Auto merge in progress'
            if check_for_merge_conflict and has_merge_conflict(hash, target_branch, remote):
                status = 'Conflict'
        print(f'  * {commit}: {status}')

    print_commit_status(commits[0], True)
    if list_commits:
        for commit in commits[1:]:
            print_commit_status(commit)
        return
    # Show an abbreviated list of commits.
    if len(commits) > 2:
        print(f'    ... {len(commits) - 2} commits in-between ...')
    if len(commits) > 1:
        print_commit_status(commits[-1])


def print_zippered_edge_status(config: AMTargetBranchConfig, remote: str):
    click.echo(click.style(
               f'[{config.upstream} -> {config.target} <- {config.secondary_upstream}]',
               bold=True))
    print(f'- This is a zippered merge branch!')

    merges: Optional[List[List[str]]] = []
    merges = compute_zippered_merges(remote=remote, target=config.target,
                                     left_upstream=config.upstream,
                                     right_upstream=config.secondary_upstream,
                                     common_ancestor=config.common_ancestor,
                                     stop_on_first=True)
    if not merges:
        # FIXME: This might not be true.
        # print(f'- There are no unmerged commits. The {config.target} branch is up to date.')
        print(f'- The status is not computable yet.')
        return

    print(f'- There is at least one merge that can be performed.')


def print_status(remote: str = 'origin', target_branch: Optional[str] = None, list_commits: bool = False,
                 query_ci_status: bool = False):
    configs: List[AMTargetBranchConfig] = find_am_configs(remote)
    if target_branch:
        configs = list(
            filter(lambda config: config.target == target_branch, configs))
    if len(configs) == 0:
        msg = ''
        if target_branch:
            msg = f'for branch "{target_branch}" from '
        print(f'No automerger configured for {msg}remote "{remote}"')
        return
    ms = find_inflight_merges(remote)
    printed = False
    for config in configs:
        if printed:
            print('')
        if config.secondary_upstream:
            print_zippered_edge_status(config, remote)
            printed = True
            continue
        print_edge_status(config.upstream, config.target,
                          ms, list_commits, remote,
                          query_ci_status=query_ci_status)
        printed = True
