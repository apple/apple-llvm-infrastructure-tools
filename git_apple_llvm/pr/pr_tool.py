import abc
from typing import List
from enum import Enum


class PullRequestState(Enum):
    Open = 1
    Merged = 2
    Closed = 3


class PullRequest(abc.ABC):
    """
      An abstract class that represents a pull request.
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


class PRTool(abc.ABC):
    """
      An abstract class that represents operations that can be
      performed on pull requests.
    """

    def list(self) -> List[PullRequest]:
        """ Return the list of pull requests. """
        pass
