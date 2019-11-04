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

    am_config = [{
        'target': 'master',
        'upstream': 'upstream'
    }]
    git('init', git_dir=path)
    os.mkdir(os.path.join(path, 'apple-llvm-config'))
    os.mkdir(os.path.join(path, 'apple-llvm-config', 'am'))
    with open(os.path.join(path, 'apple-llvm-config', 'am', 'am-config.json'), 'w') as f:
        f.write(json.dumps(am_config))
    git('add', 'apple-llvm-config/am/am-config.json', git_dir=path)
    git('commit', '-m', 'am config', git_dir=path)
    with open(os.path.join(path, 'foo'), 'w') as f:
        f.write('foo')
    git('add', 'foo', git_dir=path)
    git('commit', '-m', 'up', git_dir=path)
    git('checkout', '-b', 'repo/apple-llvm-config/am', git_dir=path)
    git('checkout', '-b', 'upstream', 'HEAD~1', git_dir=path)
    with open(os.path.join(path, 'foo'), 'w') as f:
        f.write('bar')
    git('add', 'foo', git_dir=path)
    git('commit', '-m', 'down', git_dir=path)
    return path


@pytest.fixture(scope='session')
def am_tool_git_repo_clone(tmp_path_factory, am_tool_git_repo: str) -> str:
    path = str(tmp_path_factory.mktemp('simple-am-tool-dir-clone'))
    git('init', git_dir=path)
    git('remote', 'add', 'origin', am_tool_git_repo, git_dir=path)
    git('fetch', 'origin', git_dir=path)
    git('checkout', 'master', git_dir=path)
    return path


@pytest.fixture(scope='function')
def cd_to_am_tool_repo_clone(am_tool_git_repo_clone: str):
    prev = os.getcwd()
    os.chdir(am_tool_git_repo_clone)
    yield
    os.chdir(prev)


def test_am_merge_conflict(cd_to_am_tool_repo_clone):
    result = CliRunner().invoke(am, ['status', '--target', 'master', '--no-fetch'],
                                mix_stderr=True)

    assert result.exit_code == 0
    assert 'Conflict' in result.output
