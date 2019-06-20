"""
  Tests for the git commit graph merger (used by git apple-llvm push).
"""

import pytest
from git_tools import git, git_output
from monorepo_test_harness import commit_file
from git_tools.push import CommitGraph, merge_commit_graph_with_top_of_branch, MergeStrategy, ImpossibleMergeError


@pytest.fixture(scope='function')
def split_clang_head_in_monorepo(cd_to_monorepo) -> str:
    clang_head = git_output('rev-parse', 'split/clang/internal/master')
    git('checkout', '--detach', clang_head)
    return clang_head


@pytest.fixture(scope='function',
                params=[MergeStrategy.FastForwardOnly,
                        MergeStrategy.RebaseOrMerge,
                        MergeStrategy.Rebase])
def merge_strategy(request) -> MergeStrategy:
    return request.param


def test_fast_forward_commit_graph(split_clang_head_in_monorepo: str, merge_strategy: MergeStrategy):
    # We expect a fast-forward to work with all merge strategies.
    work_head = commit_file('file1', 'updated file1')
    commit = merge_commit_graph_with_top_of_branch(CommitGraph([work_head], [split_clang_head_in_monorepo]),
                                                   'clang',
                                                   'split/clang/internal/master',
                                                   merge_strategy)
    assert work_head == commit


def test_rebase_commit_graph(split_clang_head_in_monorepo: str,
                             merge_strategy: MergeStrategy):
    work_head = commit_file('file1', 'top of branch is here')
    branch_name = f'new-rebase/split/clang/{merge_strategy}'
    git('checkout', '-b', branch_name, split_clang_head_in_monorepo)
    new_clang_head = commit_file('file2', 'top of clang/master is here')

    def doit() -> str:
        return merge_commit_graph_with_top_of_branch(CommitGraph([work_head], [split_clang_head_in_monorepo]),
                                                     'clang',
                                                     branch_name,
                                                     merge_strategy)
    # Fast forward only fails with disjoint graph.
    if merge_strategy == MergeStrategy.FastForwardOnly:
        with pytest.raises(ImpossibleMergeError) as err:
            doit()
        assert 'Not possible to fast-forward' in err.value.git_error.stderr
    else:
        # However, other strategies rebase the commit graph on top of the
        # destination branch.
        commit = doit()
        assert work_head != commit
        assert new_clang_head != commit
        assert new_clang_head == git_output('rev-parse', f'{commit}~1')


def test_merge_commit_graph(split_clang_head_in_monorepo: str,
                            merge_strategy: MergeStrategy):
    # Create the test scenario:
    # * merge                         <-- top of commit graph.
    # |\
    # |   * [new-merge/split/clang]   <-- destination.
    # | \/
    # | * file2                       <-- root of commit graph.
    # * | file1
    # |/
    # * [split/clang/internal/master] <-- root of commit graph.
    push_clang_commit = commit_file('file1', 'top of branch is here')
    branch_name = f'new-merge/split/clang/{merge_strategy}'
    git('checkout', '-b', branch_name, split_clang_head_in_monorepo)
    up_clang_head = commit_file('file2', 'merging this in')
    new_clang_head = commit_file('file3', 'top of clang/master is here')

    git('checkout', '--detach', up_clang_head)
    git('clean', '-d', '-f')
    git('merge', push_clang_commit)
    merge_commit = git_output('rev-parse', 'HEAD')

    def doit() -> str:
        graph = CommitGraph([merge_commit, push_clang_commit], [up_clang_head, split_clang_head_in_monorepo])
        return merge_commit_graph_with_top_of_branch(graph,
                                                     'clang',
                                                     branch_name,
                                                     merge_strategy)
    # Graph with merges can only be merged.
    if merge_strategy == MergeStrategy.RebaseOrMerge:
        commit = doit()
        assert merge_commit != commit

        parents = git_output('show', '--format=%P', commit).split()
        assert 2 == len(parents)
        assert new_clang_head == parents[0]
        assert merge_commit == parents[1]
    else:
        with pytest.raises(ImpossibleMergeError):
            doit()


def test_merge_conflict(split_clang_head_in_monorepo: str,
                        merge_strategy: MergeStrategy):
    push_clang_commit = commit_file('file1', 'top of branch is here')
    branch_name = f'rebase-fail/split/clang/{merge_strategy}'
    git('checkout', '-b', branch_name, split_clang_head_in_monorepo)
    commit_file('file1', 'rebase this without conflict')

    graph = CommitGraph([push_clang_commit], [split_clang_head_in_monorepo])
    with pytest.raises(ImpossibleMergeError) as err:
        merge_commit_graph_with_top_of_branch(graph,
                                              'clang',
                                              branch_name,
                                              merge_strategy)
    if merge_strategy == MergeStrategy.Rebase:
        assert err.value.operation == 'rebase'
    elif merge_strategy == MergeStrategy.RebaseOrMerge:
        assert err.value.operation == 'merge'
