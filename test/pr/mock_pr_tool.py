"""
  Mock PR tool class.
"""

from git_apple_llvm.pr.pr_tool import PullRequestState, PRTool
from typing import List, Optional


class MockPullRequest:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class MockPRTool(PRTool):
    def __init__(self, default_author: str = "<author>"):
        self.default_author = default_author
        self.pull_requests: List[MockPullRequest] = []

    def create_pull_request(self, title: str, text: str, base_branch: str, author: Optional[str] = None):
        number = len(self.pull_requests) + 1
        self.pull_requests.append(MockPullRequest(number=number,
                                                  state=PullRequestState.Open, title=title, body_text=text,
                                                  author_username=author if author else self.default_author,
                                                  base_branch=base_branch, url=f'test/pr/{number}'))

    def list(self):
        return self.pull_requests
