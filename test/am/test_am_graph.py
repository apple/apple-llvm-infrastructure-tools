"""
  Tests for the AM status files.
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
    git('commit', '-m', 'up', '--allow-empty', git_dir=path)
    git('checkout', '-b', 'repo/apple-llvm-config/am', git_dir=path)
    git('checkout', '-b', 'upstream', 'HEAD~1', git_dir=path)
    git('commit', '-m', 'down', '--allow-empty', git_dir=path)
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


def test_am_graph_ci_state(mocker, cd_to_am_tool_repo_clone):
    def fake_redis(arg):
        return 'PASSED'

    mocker.patch('git_apple_llvm.am.oracle.get_state', side_effect=fake_redis)

    result = CliRunner().invoke(
        am, ['graph', '--no-fetch', '--ci-status', '--format', 'dot'],
        mix_stderr=True)

    assert result.exit_code == 0
    assert 'subgraph cluster_LLVM' in result.output
    assert 'subgraph cluster_Github' in result.output
    assert 'subgraph cluster_Internal' in result.output
    assert 'upstream -> master' in result.output
