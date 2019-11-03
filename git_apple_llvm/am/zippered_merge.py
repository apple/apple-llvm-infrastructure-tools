"""
  Module for performing a zippered merge between two branches.

  A zippered merge is used to merge branches based on a matching
  merge-base commit from the common ancestor. For example,
  given the following commit graph:

  ancestor -> left   - > rejoin
           -> right  /

  the zippered merge would merge commits from left and right into
  the rejoin edge, as long as their ancestor merge base allows them
  to be rejoined. In particular, the following scenarios are allowed:

  - A commit from left can be merged directly when
    the commit's merge base is the same as the existing merge base of right.

  - A commit from right can be merged directly when
    the commit's merge base is the same as the existing merge base of left.

  - A commit from left and right should be merged together when
    both of them have a matching merge base.

  The algorithm favours the left for both direct merges (left's direct
  merges are performed before the right), and when looking for the next
  matching merge base between the left and right branch.
"""

from typing import List, Optional


# FIXME: The current iterator requires computing all of the merge bases upfront.
# If there's a long list of commits then computing all of these merge bases will be
# quite expensive, when probably you just need to know the "next" merge. We could
# potentially use a generator pattern instead to avoid this.
class BranchIterator:
    def __init__(self, commits: List[str], merge_bases: List[str], initial_merge_base: str):
        self.commits: List[str] = commits
        self.merge_bases: List[str] = merge_bases
        assert len(self.commits) == len(self.merge_bases)
        self.i: int = 0
        self.len: int = len(self.commits)
        self.initial_merge_base: str = initial_merge_base

    @property
    def has_commits(self) -> bool:
        return self.i < self.len

    @property
    def prev_merge_base(self) -> str:
        return self.initial_merge_base if self.i == 0 else self.merge_bases[self.i - 1]

    @property
    def current_merge_base(self) -> str:
        return self.merge_bases[self.i]

    def takeCommit(self) -> str:
        result = self.commits[self.i]
        self.i += 1
        return result

    def find_matching_merge_base(self, right) -> Optional[int]:
        i = self.i
        while i < self.len:
            if self.merge_bases[i] == right.current_merge_base:
                return i
            i += 1
        return None


def find_next_matching_merge_base(left: BranchIterator, right: BranchIterator) -> bool:
    while left.has_commits and right.has_commits:
        # Try looking through the left side, until we find a matching right
        # merge base.
        left_match: Optional[int] = left.find_matching_merge_base(right)
        if left_match is not None:
            left.i = left_match
            return True
        # No commit merge base found, advance the right.
        right.takeCommit()
    return False


def compute_zippered_merge_commits(left: BranchIterator, right: BranchIterator) -> List[List[str]]:
    """
       Return the list of lists that contain the parents of the merge commit that should
       be constructed to perform the required zippered merges.
    """
    merges: List[List[str]] = []

    while left.has_commits or right.has_commits:
        # Try merging commits from one branch first, while the merge base allows it.
        if left.has_commits and right.prev_merge_base == left.current_merge_base:
            merges.append([left.takeCommit()])
            continue
        if right.has_commits and left.prev_merge_base == right.current_merge_base:
            merges.append([right.takeCommit()])
            continue

        if find_next_matching_merge_base(left, right):
            # If both merge bases match, construct a merge from both branches in one commit.
            assert left.current_merge_base == right.current_merge_base
            merges.append([left.takeCommit(), right.takeCommit()])
            continue
        break

    return merges
