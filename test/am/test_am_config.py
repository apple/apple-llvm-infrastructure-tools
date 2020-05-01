"""
  Tests for the AM config files.
"""

import os
import pytest
from git_apple_llvm.am.am_config import find_am_configs
from git_apple_llvm.am.am_status import print_status
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
    }, {
        'target': 'swift/master',
        'upstream': 'master',
        'test_commits_in_bundle': True
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
    git('checkout', '-b', 'swift/master', 'master', git_dir=path)
    git('commit', '-m', 'up', '--allow-empty', git_dir=path)
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


def test_am_config(cd_to_am_tool_repo_clone):
    configs = find_am_configs()
    assert len(configs) == 2
    assert configs[0].upstream == 'upstream'
    assert configs[0].target == 'master'
    assert configs[0].test_command is None
    assert configs[0].secondary_upstream is None
    assert configs[0].common_ancestor is None
    assert configs[0].test_commits_in_bundle is False

    assert configs[1].upstream == 'master'
    assert configs[1].target == 'swift/master'
    assert configs[1].test_command is None
    assert configs[1].secondary_upstream is None
    assert configs[1].common_ancestor is None
    assert configs[1].test_commits_in_bundle is True


def test_am_print_status(cd_to_am_tool_repo_clone, capfd):
    print_status()
    captured = capfd.readouterr()
    assert '[upstream -> master]\n- 0 unmerged commits. master is up to date.\n' in captured.out


def test_am_status(cd_to_am_tool_repo_clone):
    result = CliRunner().invoke(am, ['status', '--target', 'master', '--no-fetch'])

    assert result.exit_code == 0
    assert result.output == '[upstream -> master]\n- 0 unmerged commits. master is up to date.\n'


# FIXME: more tests.
