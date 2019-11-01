import logging
from typing import Dict
import json
from git_apple_llvm.ci.jenkins_ci import JenkinsCIConfig
from git_apple_llvm.git_tools import read_file_or_none

log = logging.getLogger(__name__)


class TestPlan:
    """
    Represents a test plan that can be used for a pull request.

    e.g. JSON representation:
    "check-llvm": {
               "description": "Runs lit and unit tests for LLVM",
                "infer-from-changes": [
                    "llvm"
                ],
                "config": "pull-request-RA",
                "params": {
                    "monorepo_projects": "",
                    "test_targets": "check-llvm"
                }
    },

    Attributes
    ----------
    name : str

    description : str

    ci_jobs : str
    The configuration file to use to dispatch the CI jobs.
    params : Dict[str, str]
    The parameters to pass to the CI jobs.
    infer_from_dirs : List[str]
    The set of directories from which this test plan can be inferred to run on.
    """

    def __init__(self, name: str, json: Dict):
        self.name = name
        self.description = json['description']
        self.ci_jobs = json['ci-jobs']
        self.params = json['params']
        self.infer_from_dirs = json['infer-from-changes'] if 'infer-from-changes' in json else []

    def dispatch(self, params):
        ci_job_config_filename = f'apple-llvm-config/ci-jobs/{self.ci_jobs}.json'
        log.debug('Test plan %s: loading ci config %s',
                  self.name, ci_job_config_filename)
        file_contents = read_file_or_none('origin/repo/apple-llvm-config/pr',
                                          ci_job_config_filename)
        if not file_contents:
            raise RuntimeError(f'ci config {ci_job_config_filename} not found')
        ci_job_config_json = json.loads(file_contents)
        ci_job_config = JenkinsCIConfig(ci_job_config_json)
        params.update(self.params)
        log.debug(
            'Test plan %s: dispatching ci job requests for params: %s', self.name, params)
        ci_job_config.dispatch(params, self.name)


class TestPlanNotFoundError(Exception):
    """
    An exception thrown if the test plan is not defined.
    """

    def __init__(self, name: str):
        self.name = name


class TestPlanDispatcher:
    """ Dispatches test plans for pull requests with underlying CI jobs """

    def dispatch_test_plan_for_pull_request(self, name: str, pr_number: int):
        """ Loads a test plan and dispatches it for a given pull request. """
        test_plans_filename = 'apple-llvm-config/ci-test-plans.json'
        log.debug('Test plan dispatcher: loading test plans %s',
                  test_plans_filename)
        file_contents = read_file_or_none('origin/repo/apple-llvm-config/pr', test_plans_filename)
        if not file_contents:
            raise TestPlanNotFoundError(name)
        test_plans: Dict[str, TestPlan] = {}
        tp = json.loads(file_contents)['test-plans']
        for k in tp:
            test_plans[k] = TestPlan(k, tp[k])
        if name not in test_plans:
            raise TestPlanNotFoundError(name)
        log.debug(
            'Test plan dispatcher: invoking %s for pull request #%s', name, str(pr_number))
        test_plans[name].dispatch({'pullRequestID': str(pr_number)})
