"""
  Pytest fixtures that generate monorepos.
"""


import os
import pytest
import json
from git_apple_llvm.git_tools import git, git_output
import monorepo_test_harness


class MonorepoFixture:
    """ Represents a test monorepo which can be interacted with. """

    def __init__(self, path: str, clone_path: str,
                 clang_split_remote_path: str,
                 llvm_split_remote_path: str,
                 root_split_remote_path: str):
        self.path = path
        self.clone_path = clone_path
        self.clang_split_remote_path = clang_split_remote_path
        self.llvm_split_remote_path = llvm_split_remote_path
        self.root_split_remote_path = root_split_remote_path

        # Configure the push configuration.
        push_config = {
            'branch_to_dest_branch_mapping': {
                'internal/master:-': 'internal/master',
                'internal/master:*': 'master'
            },
            'repo_mapping': {
                'clang': self.clang_split_remote_path,
                'llvm': self.llvm_split_remote_path,
                '-': self.root_split_remote_path
            }
        }
        push_config_json = json.dumps(push_config)

        # Create the repo.
        cwd = os.getcwd()
        os.chdir(self.path)
        git('init')
        monorepo_test_harness.create_simple_test_harness(push_config_json=push_config_json)
        self.internal_head = git_output('rev-parse', 'internal/master')

        # Create the clone.
        os.chdir(self.clone_path)
        git('init')
        git('remote', 'add', 'origin', self.path)
        git('fetch', 'origin')

        # Create the split clones.
        def create_split_remote(path: str, split_dir: str, branch_name: str):
            os.chdir(path)
            git('init', '--bare')
            git('remote', 'add', 'origin', self.path)
            git('fetch', 'origin')
            git('branch', '-f', branch_name, f'origin/split/{split_dir}/internal/master')

        create_split_remote(self.clang_split_remote_path, 'clang', 'master')
        create_split_remote(self.llvm_split_remote_path, 'llvm', 'master')
        create_split_remote(self.root_split_remote_path, '-', 'internal/master')

        # Back to the original CWD.
        os.chdir(cwd)

    def checkout_internal_master(self):
        # Checkout the 'internal/master' branch as it was created.
        git('checkout', '--detach', self.internal_head)
        git('branch', '-f', 'internal/master', self.internal_head)
        git('checkout', 'internal/master')
        git('clean', '-d', '-f')


@pytest.fixture(scope='session')
def monorepo_test_fixture(tmp_path_factory) -> MonorepoFixture:
    path = tmp_path_factory.mktemp('simple-monorepo')
    clone_path = tmp_path_factory.mktemp('simple-monorepo-clone')
    clang_split_remote_path = tmp_path_factory.mktemp('simple-monorepo-clang-split')
    llvm_split_remote_path = tmp_path_factory.mktemp('simple-monorepo-llvm-split')
    root_split_remote_path = tmp_path_factory.mktemp('simple-monorepo-root-split')
    return MonorepoFixture(str(path), str(clone_path),
                           str(clang_split_remote_path),
                           str(llvm_split_remote_path),
                           str(root_split_remote_path))


@pytest.fixture(scope='function')
def cd_to_monorepo(monorepo_test_fixture):
    """
     This fixture provides a monorepo with a rather simple
     upstream & downstream history. It is always checked out into
     internal/master.
    """
    os.chdir(monorepo_test_fixture.path)
    monorepo_test_fixture.checkout_internal_master()


@pytest.fixture(scope='function')
def cd_to_monorepo_clone(monorepo_test_fixture):
    """
      This fixture provides a clone of monorepo with a rather simple
      upstream & downstream history. It is always checked out into
      internal/master.
    """
    os.chdir(monorepo_test_fixture.clone_path)
    monorepo_test_fixture.checkout_internal_master()
    git('branch', '-u', 'origin/internal/master', 'internal/master')


@pytest.fixture(scope='function')
def monorepo_simple_clang_remote_git_dir(monorepo_test_fixture) -> str:
    """
    Return a path to the git directory which acts as the remote for the
    'clang' split repo.
    """
    return monorepo_test_fixture.clang_split_remote_path


@pytest.fixture(scope='function')
def monorepo_simple_llvm_remote_git_dir(monorepo_test_fixture) -> str:
    """
    Return a path to the git directory which acts as the remote for the
    'llvm' split repo.
    """
    return monorepo_test_fixture.llvm_split_remote_path


@pytest.fixture(scope='function')
def monorepo_simple_root_remote_git_dir(monorepo_test_fixture) -> str:
    """
    Return a path to the git directory which acts as the remote for the
    monorepo root split repo.
    """
    return monorepo_test_fixture.root_split_remote_path
