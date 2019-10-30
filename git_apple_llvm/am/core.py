"""
  Module for core automerger operations.
"""

from git_apple_llvm.git_tools import git_output
from typing import Optional


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
