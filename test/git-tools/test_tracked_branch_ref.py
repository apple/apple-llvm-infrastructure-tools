"""
  Tests for the tracked branch ref (used by git apple-llvm pr).
"""

import pytest
from git_apple_llvm.git_tools import git
from git_apple_llvm.git_tools.tracked_branch_ref import TrackedBranchRef, get_tracked_branch_ref
from typing import Optional


@pytest.fixture(scope='function',
                params=['origin/',
                        ''])
def optional_remote_prefix(request) -> str:
    return request.param


def test_get_tracked_branch_ref(cd_to_monorepo_clone, monorepo_test_fixture, optional_remote_prefix: str):
    ref: Optional[TrackedBranchRef] = get_tracked_branch_ref(optional_remote_prefix + 'internal/master')
    assert ref is not None
    assert ref.remote_name == 'origin'
    assert ref.remote_url == monorepo_test_fixture.path
    assert ref.branch_name == 'internal/master'
    assert ref.head_hash is not None


def test_different_tracked_branch_ref(cd_to_monorepo_clone, monorepo_test_fixture, optional_remote_prefix: str):
    branch_name = 'test-tracked-branch'
    git('branch', '-D', branch_name, ignore_error=True)
    git('checkout', '-b', branch_name)
    git('branch', '-u', 'origin/internal/master', branch_name)
    ref: Optional[TrackedBranchRef] = get_tracked_branch_ref(optional_remote_prefix + branch_name)
    if len(optional_remote_prefix) > 0:
        # The ref doesn't exist on the remote
        assert ref is None
        return
    assert ref is not None
    assert ref.remote_name == 'origin'
    assert ref.remote_url == monorepo_test_fixture.path
    assert ref.branch_name == 'internal/master'


def test_get_no_tracked_branch_ref(cd_to_monorepo_clone, optional_remote_prefix: str):
    ref: Optional[TrackedBranchRef] = get_tracked_branch_ref(optional_remote_prefix + 'foo')
    assert ref is None
