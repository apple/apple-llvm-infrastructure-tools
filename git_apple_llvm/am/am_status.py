"""
  Module for computing the automerger status.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.git_tools import git, git_output
import logging
import click
from typing import Dict, List, Set, Optional
from functools import cmp_to_key


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
        if commit in commits_inflight:
            return len(commits) - not_seen
        not_seen += 1
    return 0


def print_edge_status(upstream_branch: str, target_branch: str,
                      inflight_merges: Dict[str, List[str]], list_commits: bool = False, remote: str = 'origin'):
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
        print(f'- There are no unmerged commits. The {target_branch} is up to date.')
        return
    commits: List[str] = commit_log_output.split('\n')
    inflight_count = compute_inflight_commit_count(commits, commits_inflight)
    str2 = f'{inflight_count} commits are currently being merged/build/tested.'
    print(f'- There are {len(commits)} unmerged commits. {str2}')
    print('- Unmerged commits:')

    def print_commit_status(commit: str):
        if commit in commits_inflight:
            print(f'  * {commit}: Auto merge in progress')
        else:
            print(f'  * {commit}')

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


def print_status(remote: str = 'origin', target_branch: Optional[str] = None, list_commits: bool = False):
    found_configs: List[AMTargetBranchConfig] = find_am_configs(remote)

    # Find roots.
    targets: Dict[str, bool] = {}
    upstream_config_mapping: Dict[str, List[AMTargetBranchConfig]] = {}
    # Map branch name -> config.
    for config in found_configs:
        if config.upstream in upstream_config_mapping:
            upstream_config_mapping[config.upstream].append(config)
        else:
            upstream_config_mapping[config.upstream] = [config]
        if config.upstream not in targets:
            targets[config.upstream] = False
        targets[config.target] = True

    root_configs: List[AMTargetBranchConfig] = []
    for (branch, is_target) in targets.items():
        if not is_target:
            assert len(upstream_config_mapping[branch]) == 1
            root_configs.append(upstream_config_mapping[branch][0])
    log.debug(f'ROOTS UNSORTED: {root_configs}')

    # Sort roots.
    first_one = set(['llvm.org/master', 'apple/master'])

    def compare_root(x: AMTargetBranchConfig, y: AMTargetBranchConfig):
        if x.upstream in first_one:
            return -1
        if y.upstream in first_one:
            return 1
        if x.target < y.target:
            return -1
        if y.target < x.target:
            return 1
        return 0
    root_configs = sorted(root_configs, key=cmp_to_key(compare_root))
    log.debug(f'ROOTS SORTED: {root_configs}')

    # Build topology.
    configs: List[AMTargetBranchConfig] = []
    for config in root_configs:
        configs.append(config)
        if config.target in upstream_config_mapping:
            # FIXME: More fixes!
            configs += upstream_config_mapping[config.target]

    log.debug(f'CONFIGS: {config}')

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
                          ms, list_commits, remote)
        if config.secondary_upstream:
            print_edge_status(config.secondary_upstream,
                              config.target, ms, list_commits, remote)
        printed = True
