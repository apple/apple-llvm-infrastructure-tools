"""
  Tests for the zippered merge algorithm.
"""

from git_apple_llvm.am.zippered_merge import BranchIterator, compute_zippered_merge_commits
from typing import Dict


def br(commits, merge_bases, initial_merge_base) -> BranchIterator:
    commit_to_merge_base: Dict[str, str] = {}
    for i in range(0, len(commits)):
        commit_to_merge_base[commits[i]] = merge_bases[i]
    return BranchIterator(commits, lambda commit: commit_to_merge_base[commit], initial_merge_base)


def test_zippered_merge_alg_no_zipper():
    assert compute_zippered_merge_commits(br([], [], 'm/A'),
                                          br([], [], 'm/A')) == []

    # Allow direct merges when merge bases match.
    assert compute_zippered_merge_commits(br(['l/A'], ['m/A'], 'm/A'),
                                          br([], [], 'm/A')) == [['l/A']]

    assert compute_zippered_merge_commits(br([], [], 'm/A'),
                                          br(['r/A'], ['m/A'], 'm/A')) == [['r/A']]

    assert compute_zippered_merge_commits(br(['l/A'], ['m/A'], 'm/A'),
                                          br(['r/A'], ['m/A'], 'm/A')) == [['l/A'], ['r/A']]

    # Mismatching merge bases don't allow direct merges.
    assert compute_zippered_merge_commits(br(['l/A'], ['m/A'], 'm/A'),
                                          br([], [], 'm/B')) == []

    assert compute_zippered_merge_commits(br([], [], 'm/B'),
                                          br(['r/A'], ['m/A'], 'm/A')) == []

    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/A', 'm/B'], 'm/A'),
                                          br(['r/A'], ['m/A'], 'm/A')) == [['l/A'], ['r/A']]

    assert compute_zippered_merge_commits(br(['l/A'], ['m/A'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/A', 'm/B'], 'm/A')) == [['l/A'], ['r/A']]


def test_zippered_merge_alg():
    assert compute_zippered_merge_commits(br(['l/A'], ['m/B'], 'm/A'),
                                          br(['r/A'], ['m/B'], 'm/A')) == [['l/A', 'r/A']]
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/A', 'm/B'], 'm/A'),
                                          br(['r/A'], ['m/B'], 'm/B')) == [['l/B', 'r/A']]
    assert compute_zippered_merge_commits(br(['l/A'], ['m/B'], 'm/B'),
                                          br(['r/A', 'r/B'], ['m/A', 'm/B'], 'm/A')) == [['l/A', 'r/B']]

    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/B', 'm/C'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/B', 'm/C'], 'm/A')) == [['l/A', 'r/A'], ['l/B', 'r/B']]
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/B1', 'm/C'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/B2', 'm/C'], 'm/A')) == [['l/B', 'r/B']]

    # Zippered + direct.
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/B', 'm/B'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/B', 'm/B'], 'm/A')) == [['l/A', 'r/A'],
                                                                                         ['l/B'], ['r/B']]
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/B', 'm/B'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/B', 'm/C'], 'm/A')) == [['l/A', 'r/A'], ['l/B']]
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/B', 'm/C'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/B', 'm/B'], 'm/A')) == [['l/A', 'r/A'], ['r/B']]

    # Direct + zippered.
    assert compute_zippered_merge_commits(br(['l/A', 'l/B'], ['m/A', 'm/B'], 'm/A'),
                                          br(['r/B'], ['m/B'], 'm/A')) == [['l/A'], ['l/B', 'r/B']]
    assert compute_zippered_merge_commits(br(['l/B'], ['m/B'], 'm/A'),
                                          br(['r/A', 'r/B'], ['m/A', 'm/B'], 'm/A')) == [['r/A'], ['l/B', 'r/B']]
