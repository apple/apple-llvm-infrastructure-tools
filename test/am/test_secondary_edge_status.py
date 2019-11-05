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
    result = CliRunner().invoke(am, ['status', '--target', 'downstream/swift/master', '--no-fetch'],
                                mix_stderr=True)

    assert result.exit_code == 0
    assert """[downstream/master -> downstream/swift/master <- swift/master]
- This is a zippered merge branch!
- There is at least one merge that can be performed.""" in result.output
