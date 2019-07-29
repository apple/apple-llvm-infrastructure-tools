""" A module that resolves the tracking branch information from a branch name """

from git_apple_llvm.git_tools import git_output
from typing import Optional, List


class TrackedBranchRef:
    """ Represents a reference to a git branch that's tracked remotely """

    def __init__(self, remote_name: str, remote_url: str, branch_name: str, head_hash: str):
        self.remote_name = remote_name
        self.remote_url = remote_url
        self.branch_name = branch_name
        self.head_hash = head_hash


def get_tracked_branch_ref(branch_name: str) -> Optional[TrackedBranchRef]:
    """ Returns a valid TrackedBranchRef if the given branch resolves to one """
    tracking_branch_name: Optional[str] = git_output('rev-parse', '--abbrev-ref',
                                                     '--symbolic-full-name', branch_name + '@{u}',
                                                     ignore_error=True)
    if not tracking_branch_name:
        if '/' in branch_name:
            tracking_branch_name = branch_name
        else:
            return None
    assert '/' in tracking_branch_name
    rb = tracking_branch_name.split('/', 1)
    remote_branch = rb[1]
    output: Optional[str] = git_output(
        'ls-remote', '--exit-code', rb[0], remote_branch, ignore_error=True)
    if not output:
        return None
    hash_refname: List[str] = output.split()
    if len(hash_refname) >= 2 and hash_refname[1] == f'refs/heads/{remote_branch}':
        return TrackedBranchRef(rb[0], git_output('remote', 'get-url', rb[0]), remote_branch, hash_refname[0])
    return None
