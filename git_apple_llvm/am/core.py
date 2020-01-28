"""
  Module for core automerger operations.
"""

from git_apple_llvm.git_tools import git, git_output, get_dev_null, GitError
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


def has_merge_conflict(commit: str, target_branch: str, remote: str = 'origin') -> bool:
    """ Returns true if the given commit hash has a merge conflict with the given target branch.
    """
    try:
        # Always remove the temporary worktree. It's possible that we got
        # interrupted and left it around. This will raise an exception if the
        # worktree doesn't exist, which can be safely ignored.
        git('worktree', 'remove', '--force', '.git/temp-worktree',
            stdout=get_dev_null(), stderr=get_dev_null())
    except GitError:
        pass
    git('worktree', 'add', '.git/temp-worktree', f'{remote}/{target_branch}', '--detach',
        stdout=get_dev_null(), stderr=get_dev_null())
    try:
        git('merge', '--no-commit', commit,
            git_dir='.git/temp-worktree', stdout=get_dev_null(), stderr=get_dev_null())
        return False
    except GitError:
        return True
    finally:
        git('worktree', 'remove', '--force', '.git/temp-worktree',
            stdout=get_dev_null(), stderr=get_dev_null())


def compute_unmerged_commits(remote: str, target_branch: str,
                             upstream_branch: str, format: str = '%H') -> Optional[List[str]]:
    """ Returns the list of commits that are not yet merged from upstream to the target branch. """
    commit_log_output = git_output(
        'log',
        '--date=local',
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
