"""
  Tests for the config files.
"""

import os
import pytest
from git_apple_llvm.pr.pr_tool import PullRequestState
from git_apple_llvm.pr.main import pr, PRToolType
from git_apple_llvm.config import write_config
import git_apple_llvm.pr.main
from mock_pr_tool import MockPRTool
from github_mock_pr_tool import create_mock_github_pr_tool
from click.testing import CliRunner
from git_apple_llvm.git_tools import git
import json
import httpretty


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
        'repo': 'apple-llvm-infrastructure-tools',
        'test': {
            'type': 'swift-ci'
        }
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


@pytest.fixture(scope='session')
def pr_tool_git_repo_clone(tmp_path_factory, pr_tool_git_repo: str) -> str:
    path = str(tmp_path_factory.mktemp('simple-pr-tool-dir-clone'))
    git('init', git_dir=path)
    git('remote', 'add', 'origin', pr_tool_git_repo, git_dir=path)
    git('fetch', 'origin', git_dir=path)
    git('checkout', 'master', git_dir=path)
    return path


@pytest.fixture(scope='function')
def cd_to_pr_tool_repo(pr_tool_git_repo: str):
    prev = os.getcwd()
    os.chdir(pr_tool_git_repo)
    yield
    os.chdir(prev)


@pytest.fixture(scope='function')
def cd_to_pr_tool_repo_clone(pr_tool_git_repo_clone: str):
    prev = os.getcwd()
    os.chdir(pr_tool_git_repo_clone)
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
    assert result.output == '''- [#1] My test
  test/pr/1

  This tests important things

'''


def test_list_target(pr_tool_type, cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    mock_tool.create_pull_request('Another 2', 'Stable only!', 'stable')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['list', '--target', 'master'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == '''- [#1] My test
  test/pr/1

  This tests important things

'''
    result = CliRunner().invoke(pr, ['list', '--target', 'stable'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == '''- [#2] Another 2
  test/pr/2

  Stable only!

'''
    result = CliRunner().invoke(pr, ['list', '--target', 'does-not-exist'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert result.output == ''


def test_cli_list_long_title(pr_tool_type, cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test ' * 20, 'This tests important things', 'master')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['list'],
                                mix_stderr=True)

    assert result.exit_code == 0
    assert result.output == '''- [#1] My test My test My test My test My test My test My test My test My test
  My test My test My test My test My test My test My test My test My test My
  test My test
  test/pr/1

  This tests important things

'''


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


def test_cli_tool_test_swift_ci(pr_tool_type, cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['test', '#1'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert 'Triggering pull request testing for pr #1 by <author>:' in result.output
    assert 'My test' in result.output
    assert 'you commented "@swift-ci please test" on the pull request' in result.output


JENKINS_TEST_API_URL = 'https://test.foo/bar'


@pytest.fixture(scope='function')
def cd_to_pr_tool_repo_clone_adjust_jenkins_ci(cd_to_pr_tool_repo):
    pr_config = {
        'type': 'github',
        'domain': 'github.com',
        'user': 'apple',
        'repo': 'apple-llvm-infrastructure-tools',
        'test': {
            'type': 'jenkins-test-plans'
        }
    }
    with open('apple-llvm-config/pr.json', 'w') as f:
        f.write(json.dumps(pr_config))
    test_plans = {
        "test-plans": {
            "pr": {
                "description": "",
                "ci-jobs": "pull-request-RA",
                "params": {
                    "test_targets": "check-llvm"
                }
            }
        }
    }
    ci_jobs = {
        "type": "jenkins",
        "url": JENKINS_TEST_API_URL,
        "jobs": [{
            "name": "a-RA",
            "url": JENKINS_TEST_API_URL + "/view/monorepo/job/pr-build-test",
            "params": {}
        }]
    }
    # Create the repo with the CI and test plan configs.
    with open(os.path.join('apple-llvm-config', 'ci-test-plans.json'), 'w') as f:
        f.write(json.dumps(test_plans))
    os.mkdir(os.path.join('apple-llvm-config/ci-jobs'))
    with open(os.path.join('apple-llvm-config/ci-jobs', 'pull-request-RA.json'), 'w') as f:
        f.write(json.dumps(ci_jobs))
    git('add', 'apple-llvm-config')
    git('commit', '-m', 'use jenkins now')
    yield
    git('reset', '--hard', 'HEAD~1')


@pytest.fixture(scope='function')
def config_dir(tmp_path):
    dir = str(tmp_path / 'git-apple-llvm')
    os.environ['GIT_APPLE_LLVM_CONFIG_DIR'] = dir
    yield dir
    del os.environ['GIT_APPLE_LLVM_CONFIG_DIR']


@httpretty.activate()
def test_cli_tool_test_jenkins_swift_plans(cd_to_pr_tool_repo_clone_adjust_jenkins_ci):
    write_config('jenkins-test.foo-bar', '{"username": "user", "token": "123"}')
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, PRToolType.GitHub)

    def request_callback(request, uri, response_headers):
        return [201, response_headers, '']

    url1 = f'{JENKINS_TEST_API_URL}/view/monorepo/job/pr-build-test/buildWithParameters?token=GIT_APPLE_LLVM'
    url1 += '&cause=started%20by%20user%20using%20git%20apple-llvm&pullRequestID=1'
    url1 += '&test_targets=check-llvm'
    httpretty.register_uri(httpretty.POST, url1,
                           body=request_callback)
    result = CliRunner().invoke(pr, ['test', '#1', '--test', 'pr'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert 'Triggering pull request testing for pr #1 by <author>:' in result.output
    assert 'My test' in result.output
    assert '✅ requested pr [a-RA] ci job for PR #1' in result.output


def test_cli_tool_test_invalid_pr(cd_to_pr_tool_repo):
    mock_tool = MockPRTool()
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, 'mock')

    result = CliRunner().invoke(pr, ['test', '#1'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'pull request #1 does not exist' in result.output


def test_cli_tool_test_closed_pr(pr_tool_type):
    mock_tool = MockPRTool()
    mock_tool.create_pull_request('My test', 'This tests important things', 'master')
    mock_tool.pull_requests[0].state = PullRequestState.Closed
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    result = CliRunner().invoke(pr, ['test', '#1'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'pull request #1 (My test) is no longer open' in result.output


def test_cli_tool_create_pr(cd_to_pr_tool_repo_clone, pr_tool_type):
    git('checkout', 'master')
    git('branch', '-D', 'pr_branch', ignore_error=True)
    git('checkout', '-b', 'pr_branch')
    with open('pr-file', 'w') as f:
        f.write('test pr file')
    git('add', 'pr-file')
    git('commit', '-m', 'pr file')

    mock_tool = MockPRTool()
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)

    # PR creation fails when the branch is not pushed.
    result = CliRunner().invoke(pr, ['create', '-m', 'test pr', '-b', 'master', '-h', 'pr_branch'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'head branch "pr_branch" is not a valid remote tracking branch' in result.output

    # PR should be create when the branch is there.
    git('push', 'origin', '-u', '-f', 'pr_branch')
    result = CliRunner().invoke(pr, ['create', '-m', 'test pr', '--base', 'master', '--head', 'pr_branch'],
                                mix_stderr=True)
    assert result.exit_code == 0
    assert 'Creating pull request:' in result.output
    assert '  pr_branch -> master on' in result.output
    assert 'Created a pull request #1 (test/pr/1)'

    created_pr = git_apple_llvm.pr.main.pr_tool.get_pr_from_number(1)
    assert created_pr.info.author_username == 'pr_branch'


def test_cli_tool_create_pr_invalid_base(cd_to_pr_tool_repo_clone, pr_tool_type):
    git('checkout', 'master')
    git('branch', '-D', 'pr_branch2', ignore_error=True)
    git('checkout', '-b', 'pr_branch2')
    git('push', 'origin', '-u', '-f', 'pr_branch2')

    mock_tool = MockPRTool()
    git_apple_llvm.pr.main.pr_tool = create_pr_tool(mock_tool, pr_tool_type)
    # PR creation fails when the branch is not pushed.
    result = CliRunner().invoke(pr, ['create', '-m', 'test pr', '-b', 'mastar', '-h', 'pr_branch2'],
                                mix_stderr=True)
    assert result.exit_code == 1
    assert 'base branch "mastar" is not a valid remote tracking branch' in result.output
