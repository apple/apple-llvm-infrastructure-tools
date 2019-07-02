"""
  Tests for the git commit graph regrafter (used by git apple-llvm push).
"""

import pytest
from git_apple_llvm.git_tools import git, git_output
from monorepo_test_harness import commit_file
from git_apple_llvm.git_tools.push import CommitGraph, regraft_commit_graph_onto_split_repo, \
    RegraftNoSplitRootError, RegraftMissingSplitRootError


def test_regraft_commit_graph(cd_to_monorepo):
    # Create the test scenario:
    # * new-dir/root-file [internal/master] <-- top of commit graph.
    # * llvm/file1
    # * clang/new-file
    # * [origin/internal/master]            <-- root of commit graph.
    internal_head = git_output('rev-parse', 'internal/master')
    clang_change_hash = commit_file('clang/new-file', 'internal: new file')
    llvm_change_hash = commit_file('llvm/file1', 'internal: rewrite file1')
    root_change_hash = commit_file('new-dir/root-file', 'internal: new file in new dir in -')

    commit_graph = CommitGraph([root_change_hash, llvm_change_hash,
                                clang_change_hash], [internal_head])

    # Verify that the expected split commit graphs have been produced.
    clang_commit_graph = regraft_commit_graph_onto_split_repo(commit_graph, 'clang')
    assert 1 == len(clang_commit_graph.roots)
    assert git_output('rev-parse', 'split/clang/internal/master') == clang_commit_graph.roots[0]
    assert 1 == len(clang_commit_graph.commits)
    assert 'new-file' == git_output('show', clang_commit_graph.commits[0], '--name-only', '--format=')

    llvm_commit_graph = regraft_commit_graph_onto_split_repo(commit_graph, 'llvm')
    assert 1 == len(llvm_commit_graph.roots)
    assert git_output('rev-parse', 'split/llvm/internal/master') == llvm_commit_graph.roots[0]
    assert 1 == len(llvm_commit_graph.commits)
    assert 'file1' == git_output('show', llvm_commit_graph.commits[0], '--name-only', '--format=')

    root_commit_graph = regraft_commit_graph_onto_split_repo(commit_graph, '-')
    assert 1 == len(root_commit_graph.roots)
    assert git_output('rev-parse', 'split/-/internal/master') == root_commit_graph.roots[0]
    assert 1 == len(root_commit_graph.commits)
    assert 'new-dir/root-file' == git_output('show', root_commit_graph.commits[0], '--name-only', '--format=')

    # "Regenerate" the clang change from the test scenario with appropriate
    # monorepo metadata.
    # * clang/new-file                      <-- [internal/master-regenerated]
    # * [origin/internal/master]            <-- root of commit graph.
    git('checkout', '-b', 'internal/master-regenerated', internal_head)
    git('clean', '-d', '-f')
    trailer = f'\napple-llvm-split-commit: {clang_commit_graph.commits[0]}\napple-llvm-split-dir: clang/'
    regenerated_clang_change_hash = commit_file('clang/new-file', 'internal: new file',
                                                trailers=trailer)

    # Create the test scenario:
    # * clang/dir/subchange                 <-- top of commit graph.
    # * merge
    # |\
    # | * [internal/master-regenerated]     <-- root of commit graph.
    # * | clang/dir/subchange
    # |/
    # * [origin/internal/master]            <-- root of commit graph.
    git('checkout', '-b', 'internal/feature', internal_head)
    git('clean', '-d', '-f')
    first_clang_change = commit_file('clang/dir/subchange', 'internal: feature file')
    git('merge', regenerated_clang_change_hash)
    clang_merge = git_output('rev-parse', 'HEAD')
    clang_change_after_merge = commit_file('clang/dir/subchange', 'internal: feature file\nnewline')

    merge_graph = CommitGraph([clang_change_after_merge, clang_merge,
                               first_clang_change], [internal_head,
                                                     regenerated_clang_change_hash])
    merged_clang_commit_graph = regraft_commit_graph_onto_split_repo(merge_graph, 'clang')
    assert 2 == len(merged_clang_commit_graph.roots)
    assert clang_commit_graph.roots[0] == merged_clang_commit_graph.roots[1]
    assert clang_commit_graph.commits[0] == merged_clang_commit_graph.roots[0]
    assert 3 == len(merged_clang_commit_graph.commits)
    assert 'dir/subchange' == git_output('show', merged_clang_commit_graph.commits[0],
                                         '--name-only', '--format=')
    assert 'dir/subchange' == git_output('show', merged_clang_commit_graph.commits[2],
                                         '--name-only', '--format=')

    # Try to regraft something unmodified in the commit graph.
    clang_only_commit_graph = CommitGraph([clang_change_hash], [internal_head])
    no_llvm_commit_graph = regraft_commit_graph_onto_split_repo(clang_only_commit_graph, 'llvm')
    assert None == no_llvm_commit_graph
    no_root_commit_graph = regraft_commit_graph_onto_split_repo(clang_only_commit_graph, '-')
    assert None == no_root_commit_graph


def test_regraft_no_split_root(cd_to_monorepo):
    internal_head = git_output('rev-parse', 'internal/master')
    clang_change_hash = commit_file('clang/new-file', 'internal: new file')

    # Try to regraft something that has no `apple-llvm-split-*` history.
    with pytest.raises(RegraftNoSplitRootError) as err:
        regraft_commit_graph_onto_split_repo(CommitGraph([clang_change_hash], [internal_head]), 'polly')
    assert err.value.root_commit_hash == internal_head


def test_regraft_missing_split_root(cd_to_monorepo):
    # Test the scenario with a missing split root.
    git('checkout', '-b', 'fake-internal-master', 'internal/master')
    git('commit', '--allow-empty', '-m', 'error\n\napple-llvm-split-commit: 00000000000\napple-llvm-split-dir: clang/')
    bad_master_head = git_output('rev-parse', 'HEAD')
    bad_clang_hash = commit_file('clang/another-file', 'internal: new file')
    with pytest.raises(RegraftMissingSplitRootError) as err:
        regraft_commit_graph_onto_split_repo(CommitGraph([bad_clang_hash], [bad_master_head]), 'clang')
    assert err.value.root_commit_hash == bad_master_head
