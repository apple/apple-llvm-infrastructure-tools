"""
  Tests for the monorepo test harness that is used by other tests.
"""

from pathlib import PosixPath
from git_apple_llvm.git_tools import git, git_output


def test_monorepo_simple_test_harness(cd_to_monorepo):
    internal_commits = git_output('rev-list', 'internal/master').splitlines()
    assert len(internal_commits) == 16
    trailers = git_output('show', '--format=%B', internal_commits[0])
    assert 'apple-llvm-split-commit:' in trailers
    assert 'apple-llvm-split-dir: -/' in trailers

    assert PosixPath('clang/dir/file2').is_file()

    upstream_commits = git_output('rev-list', 'llvm/master').splitlines()
    assert len(upstream_commits) == 9
    assert internal_commits[-1] == upstream_commits[-1]
    # Verify that each upstream commit is in downstream.
    for commit in upstream_commits:
        git('merge-base', '--is-ancestor', commit, 'internal/master')

    internal_clang_commits = git_output('rev-list', 'split/clang/internal/master').splitlines()
    assert len(internal_clang_commits) == 7

    upstream_clang_commits = git_output('rev-list', 'split/clang/upstream/master').splitlines()
    assert len(upstream_clang_commits) == 4
