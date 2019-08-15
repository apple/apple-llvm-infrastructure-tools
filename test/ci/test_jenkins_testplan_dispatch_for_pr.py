"""
  Tests for the config files.
"""

import os
import pytest
from git_apple_llvm.git_tools import git
from git_apple_llvm.config import write_config
import git_apple_llvm.ci.jenkins_ci as jenkins
import git_apple_llvm.ci.test_plans as test_plans
import json
import httpretty


TEST_API_URL = 'https://test.foo/bar'


@pytest.fixture(scope='session')
def ci_tool_git_repo(tmp_path_factory) -> str:
    path = str(tmp_path_factory.mktemp('simple-ci-tool-dir'))

    test_plans = {
        "test-plans": {
            "check-llvm": {
                "description": "Runs lit and unit tests for LLVM",
                "infer-from-changes": [
                    "llvm"
                ],
                "ci-jobs": "pull-request-RA",
                "params": {
                    "monorepo_projects": "",
                    "test_targets": "check-llvm"
                }
            }
        }
    }
    ci_jobs = {
        "type": "jenkins",
        "url": TEST_API_URL,
        "jobs": [
            {
                "name": "a-RA",
                "url": TEST_API_URL + "/view/monorepo/job/pr-build-test",
                "params": {
                    "build_variant": "a"
                }
            },
            {
                "name": "b-RA",
                "url": TEST_API_URL + "/view/monorepo/job/pr-build-test",
                "params": {
                    "build_variant": "b"
                }
            }
        ]
    }
    # Create the repo with the CI and test plan configs.
    git('init', git_dir=path)
    os.mkdir(os.path.join(path, 'apple-llvm-config'))
    with open(os.path.join(path, 'apple-llvm-config', 'ci-test-plans.json'), 'w') as f:
        f.write(json.dumps(test_plans))
    os.mkdir(os.path.join(path, 'apple-llvm-config/ci-jobs'))
    with open(os.path.join(path, 'apple-llvm-config/ci-jobs', 'pull-request-RA.json'), 'w') as f:
        f.write(json.dumps(ci_jobs))
    git('add', 'apple-llvm-config', git_dir=path)
    git('commit', '-m', 'ci config', git_dir=path)
    return path


@pytest.fixture(scope='function')
def cd_to_pr_tool_repo(ci_tool_git_repo: str):
    prev = os.getcwd()
    os.chdir(ci_tool_git_repo)
    yield
    os.chdir(prev)


@pytest.fixture(scope='function')
def config_dir(tmp_path):
    dir = str(tmp_path / 'git-apple-llvm')
    os.environ['GIT_APPLE_LLVM_CONFIG_DIR'] = dir
    yield dir
    del os.environ['GIT_APPLE_LLVM_CONFIG_DIR']


@httpretty.activate()
def test_pr_tool_list(config_dir: str, cd_to_pr_tool_repo, capfd):
    write_config('jenkins-test.foo-bar', '{"username": "user", "token": "123"}')

    def request_callback(request, uri, response_headers):
        return [201, response_headers, '']

    url1 = f'{TEST_API_URL}/view/monorepo/job/pr-build-test/buildWithParameters?token=GIT_APPLE_LLVM'
    url1 += '&cause=started%20by%20user%20using%20git%20apple-llvm&pullRequestID=9&monorepo_projects='
    url1 += '&test_targets=check-llvm&build_variant=a'
    httpretty.register_uri(httpretty.POST, url1,
                           body=request_callback,
                           match_querystring=True)
    url2 = f'{TEST_API_URL}/view/monorepo/job/pr-build-test/buildWithParameters?token=GIT_APPLE_LLVM'
    url2 += '&cause=started%20by%20user%20using%20git%20apple-llvm&pullRequestID=9&monorepo_projects='
    url2 += '&test_targets=check-llvm&build_variant=b'
    httpretty.register_uri(httpretty.POST, url2,
                           body=request_callback,
                           match_querystring=True)

    tp = test_plans.TestPlanDispatcher()
    tp.dispatch_test_plan_for_pull_request('check-llvm', 9)
    out, err = capfd.readouterr()
    assert out == """✅ requested check-llvm [a-RA] ci job for PR #9
✅ requested check-llvm [b-RA] ci job for PR #9
"""

    def request_callback_err(request, uri, response_headers):
        return [402, response_headers, 'problem on server']

    url1 = f'{TEST_API_URL}/view/monorepo/job/pr-build-test/buildWithParameters?token=GIT_APPLE_LLVM'
    url1 += '&cause=started%20by%20user%20using%20git%20apple-llvm&pullRequestID=1&monorepo_projects='
    url1 += '&test_targets=check-llvm&build_variant=a'
    httpretty.register_uri(httpretty.POST, url1,
                           body=request_callback_err,
                           match_querystring=True)
    with pytest.raises(jenkins.CIDispatchError) as err:
        tp.dispatch_test_plan_for_pull_request('check-llvm', 1)
    assert err.value.status_code == 402
    assert err.value.error == 'problem on server'
