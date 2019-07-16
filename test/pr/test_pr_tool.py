"""
  Tests for the config files.
"""

import os
import pytest
from git_apple_llvm.pr.pr_tool import PullRequestState
from git_apple_llvm.pr.main import pr, PRToolType
import git_apple_llvm.pr.main
from mock_pr_tool import MockPRTool
from github_mock_pr_tool import create_mock_github_pr_tool
from click.testing import CliRunner
from git_apple_llvm.git_tools import git
import json


@pytest.fixture(scope='function',
                params=['mock', PRToolType.GitHub])
def pr_tool_type(request) -> PRToolType:
    return request.param


def create_pr_tool(mock_tool: MockPRTool, pr_tool_type):
    if pr_tool_type == 'mock':
        return mock_tool
    if pr_tool_type == PRToolType.GitHub:
        return create_mock_github_pr_tool(mock_tool)


@pytest.fixture(scope='session')
def pr_tool_git_repo(tmp_path_factory) -> str:
    path = str(tmp_path_factory.mktemp('simple-pr-tool-dir'))

    pr_config = {
        'type': 'github',
        'domain': 'github.com',
        'user': 'apple',
        'repo': 'apple-llvm-infrastructure-tools'
    }
    pr_config_json = json.dumps(pr_config)
    # Create the repo with the PR config.
    git('init', git_dir=path)
    os.mkdir(os.path.join(path, 'apple-llvm-config'))
    with open(os.path.join(path, 'apple-llvm-config', 'pr.json'), 'w') as f:
        f.write(pr_config_json)
    git('add', 'apple-llvm-config/pr.json', git_dir=path)
    git('commit', '-m', 'pr config', git_dir=path)
    return path


@pytest.fixture(scope='function')
def cd_to_pr_tool_repo(pr_tool_git_repo: str):
    prev = os.getcwd()
    os.chdir(pr_tool_git_repo)
    yield
    os.chdir(prev)


def test_pr_tool_list(pr_tool_type):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    tool = create_pr_tool(mock_tool, pr_tool_type)
    prs = tool.list()

    assert len(prs) == 1
    assert prs[0].number == 1
    assert prs[0].state == PullRequestState.Open
    assert prs[0].title == 'My test'
    assert prs[0].body_text == 'This tests important things'
    assert prs[0].author_username == '<author>'
    assert prs[0].base_branch == 'master'


def test_cli_list(pr_tool_type, cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['list'],
                                mix_stderr=True)

    assert result.exit_code == 0
    assert result.output == '''[#1] My test

This tests important things
test/pr/1

'''


def test_list_target(pr_tool_type, cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    mock_tool.create_pull_request('Another 2', 'Stable only!', 'stable')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['list', '--target', 'master'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == '''[#1] My test

This tests important things
test/pr/1

'''
    result = CliRunner().invoke(pr, ['list', '--target', 'stable'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == '''[#2] Another 2

Stable only!
test/pr/2

'''
    result = CliRunner().invoke(pr, ['list', '--target', 'does-not-exist'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == ''


def test_cli_tool_no_git(tmp_path):
    prev = os.getcwd()
    os.chdir(str(tmp_path))
    result = CliRunner().invoke(pr, ['list'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'not a git repository' in result.output
    os.chdir(prev)


def test_cli_tool_no_pr_config(tmp_path):
    prev = os.getcwd()
    os.chdir(str(tmp_path))
    git('init')
    result = CliRunner().invoke(pr, ['list'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'missing `git apple-llvm pr` configuration file' in result.output
    os.chdir(prev)
