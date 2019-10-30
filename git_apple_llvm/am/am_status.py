"""
  Module for computing the automerger status.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import is_secondary_edge_commit_blocked_by_primary_edge
from git_apple_llvm.am.oracle import get_ci_status
from git_apple_llvm.git_tools import git, git_output
import logging
import click
from typing import Dict, List, Set, Optional


AM_PREFIX = 'refs/am/changes/'
AM_STATUS_PREFIX = 'refs/am-status/changes/'


log = logging.getLogger(__name__)


def find_inflight_merges(remote: str = 'origin') -> Dict[str, List[str]]:
    """
       This function fetches the refs created by the automerger to find
       the inflight merges that are currently being processed.
    """
    git('fetch', remote,
        f'{AM_PREFIX}*:{AM_STATUS_PREFIX}*')  # FIXME: handle fetch failures.
    refs = git_output('for-each-ref', AM_STATUS_PREFIX,
                      '--format=%(refname)').split('\n')

    inflight_merges: Dict[str, List[str]] = {}

    for ref in refs:
        if not ref:
            continue
        assert ref.startswith(AM_STATUS_PREFIX)
        merge_name = ref[len(AM_STATUS_PREFIX):]
        underscore_idx = merge_name.find('_')
        assert underscore_idx != -1
        commit_hash = merge_name[:underscore_idx]
        dest_branch = merge_name[underscore_idx + 1:]

        if dest_branch in inflight_merges:
            inflight_merges[dest_branch].append(commit_hash)
        else:
            inflight_merges[dest_branch] = [commit_hash]

    for (m, k) in inflight_merges.items():
        log.debug(f'in-flight {m}: {k}')
    return inflight_merges


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
                      common_ancestor: Optional[str] = None, primary_edge: Optional[str] = None,
                      query_ci_status: bool = False):
    commits_inflight: Set[str] = set(
        inflight_merges[target_branch] if target_branch in inflight_merges else [])

    commit_log_output = git_output(
        'log',
        '--first-parent',
        '--pretty=format:%H %cd', '--no-patch',
        f'{remote}/{target_branch}..{remote}/{upstream_branch}',
    )
    click.echo(click.style(
        f'[{upstream_branch} -> {target_branch}]', bold=True))
    if not commit_log_output:
        print(f'- There are no unmerged commits. The {target_branch} branch is up to date.')
        return
    commits: List[str] = commit_log_output.split('\n')
    inflight_count = compute_inflight_commit_count(commits, commits_inflight)
    str2 = f'{inflight_count} commits are currently being merged/build/tested.'
    print(f'- There are {len(commits)} unmerged commits. {str2}')
    print('- Unmerged commits:')

    def print_commit_status(commit: str):
        hash = commit.split(' ')[0]
        is_blocked: bool = False
        if common_ancestor:
            is_blocked = is_secondary_edge_commit_blocked_by_primary_edge(hash, f'{remote}/{common_ancestor}',
                                                                          f'{remote}/{target_branch}')
        status = ''
        if hash in commits_inflight:
            status = f'{status} Auto merge in progress'
        if is_blocked:
            status = f'{status} Blocked by not fully merged {primary_edge} -> {target_branch} edge'
        if status:
            status = f':{status}'
        if query_ci_status:
            ci_state: Optional[str] = get_ci_status(hash, target_branch)
            if ci_state:
                status = f': {ci_state}'
        print(f'  * {commit}{status}')

    print_commit_status(commits[0])
    if list_commits:
        for commit in commits[1:]:
            print_commit_status(commit)
        return
    # Show an abbreviated list of commits.
    if len(commits) > 2:
        print(f'    ... {len(commits) - 2} commits in-between ...')
    if len(commits) > 1:
        print_commit_status(commits[-1])


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
        print_edge_status(config.upstream, config.target,
                          ms, list_commits, remote,
                          query_ci_status=query_ci_status)
        if config.secondary_upstream:
            print_edge_status(config.secondary_upstream,
                              config.target, ms, list_commits, remote,
                              common_ancestor=config.common_ancestor,
                              primary_edge=config.upstream,
                              query_ci_status=query_ci_status)
        printed = True
