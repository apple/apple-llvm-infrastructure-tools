"""
  Creates a mock github PR tool.
"""

from git_apple_llvm.pr.github_pr_tool import GithubPRTool
from git_apple_llvm.pr.pr_tool import PullRequestState
from git_apple_llvm.git_tools import git_output
from mock_pr_tool import MockPRTool
from typing import List
import github3


def _convert_pr_state(state: PullRequestState) -> str:
    if state == PullRequestState.Open:
        return 'open'
    if state == PullRequestState.Merged:
        return 'merged'
    return PullRequestState.Closed


class _GitHubUser():
    def __init__(self, login: str):
        self.login = login


class _GitHubBranch():
    def __init__(self, ref: str):
        self.ref = ref


class _GitHubPullRequest(github3.pulls.ShortPullRequest):
    def __init__(self, pr):
        self.number = pr.number
        self.state = _convert_pr_state(pr.state)
        self.title = pr.title
        self.body_text = pr.body_text
        self.user = _GitHubUser(pr.author_username)
        self.base = _GitHubBranch(pr.base_branch)
        self.html_url = pr.url

    def create_comment(self, body: str):
        pass


class _GitHubRepo:
    def __init__(self, prs: List[_GitHubPullRequest]):
        self.prs = prs

    def pull_requests(self):
        return self.prs

    def pull_request(self, number: int):
        for pr in self.prs:
            if pr.number == number:
                return pr
        raise ValueError('invalid pr number')

    @property
    def clone_url(self):
        return git_output('remote', 'get-url', 'origin')

    def create_pull(self, title: str, base: str, head: str):
        mock = MockPRTool()
        mock.create_pr(title=title, base_branch=base, head_repo_url=None, head_branch=head)
        self.prs.append(_GitHubPullRequest(mock.pull_requests[0]))
        return self.prs[-1]


def create_mock_github_pr_tool(mock: MockPRTool) -> GithubPRTool:
    prs = [_GitHubPullRequest(x) for x in mock.pull_requests]
    return GithubPRTool(None, _GitHubRepo(prs))
