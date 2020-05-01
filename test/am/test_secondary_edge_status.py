"""
  Tests for the AM secondary edge status logic.
"""

import os
import pytest
from git_apple_llvm.am.main import am
from git_apple_llvm.git_tools import git
import json
from click.testing import CliRunner


@pytest.fixture(scope='session')
def am_tool_git_repo(tmp_path_factory) -> str:
    path = str(tmp_path_factory.mktemp('simple-am-dir'))

    # Merge graph:
    # master            -> swift/master
    #    |                      |
    #    \                      \
    # downstream/master -> downstream/swift/master
    am_config = [{
        'target': 'downstream/swift/master',
        'upstream': 'downstream/master',
        'secondary-upstream': 'swift/master',
        'common-ancestor': 'master'
    }, {
        'target': 'downstream/swift/master-merged',
        'upstream': 'downstream/master',
        'secondary-upstream': 'swift/master',
        'common-ancestor': 'master'
    }, {
        'target': 'downstream/swift/master-unmergeable',
        'upstream': 'downstream/master',
        'secondary-upstream': 'swift/master2',
        'common-ancestor': 'master'
    }]
    git('init', git_dir=path)
    os.mkdir(os.path.join(path, 'apple-llvm-config'))
    os.mkdir(os.path.join(path, 'apple-llvm-config', 'am'))
    with open(os.path.join(path, 'apple-llvm-config', 'am', 'am-config.json'), 'w') as f:
        f.write(json.dumps(am_config))
    git('add', 'apple-llvm-config/am/am-config.json', git_dir=path)
    git('commit', '-m', 'am config', git_dir=path)
    git('commit', '-m', 'up', '--allow-empty', git_dir=path)
    git('checkout', '-b', 'repo/apple-llvm-config/am', git_dir=path)
    git('checkout', '-b', 'downstream/master', git_dir=path)
    git('checkout', '-b', 'downstream/swift/master', 'master~1', git_dir=path)
    git('commit', '-m', 'try me 2', '--allow-empty', git_dir=path)
    git('checkout', '-b', 'swift/master', 'master~1', git_dir=path)
    git('commit', '-m', 'waiting for merges', '--allow-empty', git_dir=path)
    git('merge', 'master', git_dir=path)
    git('checkout', '-b', 'downstream/swift/master-merged', 'downstream/swift/master', git_dir=path)
    git('merge', 'downstream/master', '--no-edit', git_dir=path)
    git('merge', 'swift/master', '--no-edit', git_dir=path)
    git('checkout', '-b', 'downstream/swift/master-unmergeable', 'downstream/swift/master-merged', git_dir=path)
    git('checkout', 'master', git_dir=path)
    git('commit', '-m', 'not yet merged through one', '--allow-empty', git_dir=path)
    git('checkout', '-b', 'swift/master2', 'swift/master', git_dir=path)
    git('merge', 'master', '--no-edit', git_dir=path)
    return path


@pytest.fixture(scope='session')
def am_tool_git_repo_clone(tmp_path_factory, am_tool_git_repo: str) -> str:
    path = str(tmp_path_factory.mktemp('simple-am-tool-dir-clone'))
    git('init', git_dir=path)
    git('remote', 'add', 'origin', am_tool_git_repo, git_dir=path)
    git('fetch', 'origin', git_dir=path)
    git('checkout', 'downstream/swift/master', git_dir=path)
    return path


@pytest.fixture(scope='function')
def cd_to_am_tool_repo_clone(am_tool_git_repo_clone: str):
    prev = os.getcwd()
    os.chdir(am_tool_git_repo_clone)
    yield
    os.chdir(prev)


def test_am_secondary_edge_status(cd_to_am_tool_repo_clone):
    result = CliRunner().invoke(am, ['status', '--target', 'downstream/swift/master', '--no-fetch'])

    assert result.exit_code == 0
    assert """[downstream/master -> downstream/swift/master <- swift/master]
- This is a zippered merge branch!
- 1 unmerged commits from downstream/master.
- 2 unmerged commits from swift/master.
- The automerger has found a common merge-base.""" in result.output


def test_am_secondary_edge_status_merged(cd_to_am_tool_repo_clone):
    result = CliRunner().invoke(am, ['status', '--target', 'downstream/swift/master-merged', '--no-fetch'])

    assert result.exit_code == 0
    assert """[downstream/master -> downstream/swift/master-merged <- swift/master]
- This is a zippered merge branch!
- 0 unmerged commits. downstream/swift/master-merged is up to date.""" in result.output


def test_am_secondary_edge_status_blocked(cd_to_am_tool_repo_clone):
    result = CliRunner().invoke(am, ['status', '--target', 'downstream/swift/master-unmergeable', '--no-fetch'])

    print(result.output)
    assert result.exit_code == 0
    assert """[downstream/master -> downstream/swift/master-unmergeable <- swift/master2]
- This is a zippered merge branch!
- 0 unmerged commits from downstream/master.
- 1 unmerged commits from swift/master2.
- The automerger is waiting for unmerged commits to share
  a merge-base from master
  before merging (i.e., one of the upstreams is behind).""" in result.output
