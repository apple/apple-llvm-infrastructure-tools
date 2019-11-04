"""
  Module for core automerger operations.
"""

from git_apple_llvm.git_tools import git, git_output
from typing import Dict, List, Optional
import logging

AM_PREFIX = 'refs/am/changes/'
AM_STATUS_PREFIX = 'refs/am-status/changes/'


log = logging.getLogger(__name__)


class CommitStates:
    new = "NEW"
    conflict = "CONFLICT"
    pending = "PENDING"
    started = "STARTED"
    passed = "PASSED"
    failed = "FAILED"
    known_failed = "KNOWN_FAILED"  # When Failed was already reported.
    all = [new, conflict, pending, started, passed, failed, known_failed]


def is_secondary_edge_commit_blocked_by_primary_edge(upstream_commit_hash: str, common_ancestor_ref: str,
                                                     target_ref: str, git_dir: Optional[str] = None) -> bool:
    """ Returns true if the given commit hash from secondary upstream edge can be merged,
        iff its merge base with common ancestor has been already merged in through the
        primary upstream edge.
    """
    merge_base_hash = git_output('merge-base', upstream_commit_hash, common_ancestor_ref, git_dir=git_dir)
    # Check to see if the merge base is already in the target branch.
    br = git_output('branch', '-r', target_ref, '--contains', merge_base_hash, '--format=%(refname)', git_dir=git_dir)
    if not br:
        return True
    return br != 'refs/remotes/' + target_ref


def compute_unmerged_commits(remote: str, target_branch: str,
                             upstream_branch: str, format: str = '%H') -> Optional[List[str]]:
    """ Returns the list of commits that are not yet merged from upstream to the target branch. """
    commit_log_output = git_output(
        'log',
        '--first-parent',
        f'--pretty=format:{format}', '--no-patch',
        f'{remote}/{target_branch}..{remote}/{upstream_branch}',
    )
    if not commit_log_output:
        return None
    return commit_log_output.split('\n')


def find_inflight_merges(remote: str = 'origin') -> Dict[str, List[str]]:
    """
       This function fetches the refs created by the automerger to find
       the inflight merges that are currently being processed.
    """
    # Delete the previously fetched refs to avoid fetch failures
    # where there were force pushes.
    existing_refs = git_output('for-each-ref', AM_STATUS_PREFIX,
                               '--format=%(refname)').split('\n')
    for ref in existing_refs:
        if not ref:
            continue
        log.debug(f'Deleting local ref "{ref}" before fetching')
        git('update-ref', '-d', ref)
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
