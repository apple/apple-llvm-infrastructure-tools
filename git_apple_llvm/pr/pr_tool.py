import abc
from typing import List, Optional
from enum import Enum
import git_apple_llvm.ci.test_plans as tp


class PullRequestState(Enum):
    Open = 1
    Merged = 2
    Closed = 3


class PullRequestInfo(abc.ABC):
    """
      An abstract class that represents the info about a pull request.
    """

    @property
    def number(self) -> int:
        """ PR's numerical id. """
        pass

    @property
    def state(self) -> PullRequestState:
        pass

    @property
    def title(self) -> str:
        pass

    @property
    def body_text(self) -> str:
        pass

    @property
    def author_username(self) -> str:
        pass

    @property
    def base_branch(self) -> str:
        pass

    @property
    def url(self) -> str:
        pass


class PullRequest(abc.ABC):
    """
      An abstract class that abstracts over pull request info and actions.
    """
    @property
    def info(self) -> PullRequestInfo:
        pass

    def test_swift_ci(self):
        """ Trigger a test action using swift-ci """
        pass

    def test_jenkins_test_plans(self, test_plan_name: str):
        """ Trigger a test action using jenkins test plans """
        tp.TestPlanDispatcher().dispatch_test_plan_for_pull_request(test_plan_name, self.info.number)


class PRTool(abc.ABC):
    """
      An abstract class that represents operations that can be
      performed on pull requests.
    """

    def list(self) -> List[PullRequestInfo]:
        """ Return the list of pull requests. """
        pass

    def get_pr_from_number(self, pr_number: int) -> Optional[PullRequest]:
        """ Return the pull request or None if it doesn't exit """
        pass

    def create_pr(self, title: str, base_branch: str, head_repo_url: Optional[str], head_branch: str) -> PullRequest:
        """ Create a new pull request """
        pass
