"""
  A Github tool for working with pull requests.
"""

from git_apple_llvm.pr.pr_tool import PullRequestInfo, PullRequest, PullRequestState, PRTool
import github3
from getpass import getuser, getpass
from socket import gethostname
import json
import logging
from typing import List, Optional
from git_apple_llvm.config import read_config, write_config


log = logging.getLogger(__name__)


class GithubPullRequestInfo(PullRequestInfo):
    def __init__(self, pr: github3.pulls.ShortPullRequest):
        self.pr = pr

    @property
    def number(self) -> int:
        return self.pr.number

    @property
    def state(self) -> PullRequestState:
        s = self.pr.state
        if s == 'open':
            return PullRequestState.Open
        if s == 'merged':
            return PullRequestState.Merged
        return PullRequestState.Closed

    @property
    def title(self) -> str:
        return self.pr.title

    @property
    def body_text(self) -> str:
        return self.pr.body_text

    @property
    def author_username(self) -> str:
        return self.pr.user.login

    @property
    def base_branch(self) -> str:
        return self.pr.base.ref

    @property
    def url(self) -> str:
        return self.pr.html_url


class GithubPullRequest(PullRequest):
    def __init__(self, pr: github3.pulls.PullRequest):
        self.pr = pr

    @property
    def info(self) -> PullRequestInfo:
        return GithubPullRequestInfo(self.pr)

    def add_comment(self, text: str):
        self.pr.create_comment(body=text)

    def test(self):
        # FIXME: Support different test flavors.
        self.add_comment('@swift-ci please test')


def _doesRepoMatchURL(repo, url: str) -> bool:
    return repo.clone_url == url or repo.git_url == url or repo.ssh_url == url


class GithubPRTool(PRTool):
    def __init__(self, gh, repo):
        self.gh = gh
        self.repo = repo

    def list(self) -> List[PullRequestInfo]:
        return [GithubPullRequestInfo(x) for x in self.repo.pull_requests()]

    def get_pr_from_number(self, pr_number: int) -> Optional[PullRequest]:
        try:
            return GithubPullRequest(self.repo.pull_request(number=pr_number))
        except github3.exceptions.NotFoundError as exc:
            if exc.code == 404:
                return None
            raise exc

    def find_head_repo_owner(self, url: str) -> Optional[str]:
        if _doesRepoMatchURL(self.repo, url):
            return None
        for fork in self.repo.forks():
            if _doesRepoMatchURL(fork, url):
                return fork.owner.login
        assert False

    def create_pr(self, title: str, base_branch: str, head_repo_url: Optional[str], head_branch: str) -> PullRequest:
        # body=''
        if head_repo_url:
            head_owner = self.find_head_repo_owner(head_repo_url)
            log.debug('Found github repo fork %s for %s',
                      head_owner, head_repo_url)
            head = f'{head_owner}:{head_branch}' if head_owner else head_branch
        else:
            head = head_branch  # origin, same repo.
        pr = self.repo.create_pull(title=title, base=base_branch, head=head)
        return GithubPullRequest(pr)


def _create_access_token(domain: str, username: str, password: str):
    """
        Creates and saves the github access token.
    """
    # Use a nice name for the access token so it's clear from which
    # machine this token is used. e.g.
    # git apple-llvm for user@my-macbook.local
    note = f'git apple-llvm for {getuser()}@{gethostname()}'

    gh = github3.GitHub()
    auth = gh.authorize(username, password,
                        scopes=['repo'],
                        note=note,
                        note_url='https://github.com/apple/apple-llvm-infrastructure-tools')

    # Save the access token.
    write_config(f'pr-{domain}',
                 json.dumps({'user': username, 'token': auth.token}))
    return auth


def _load_access_token(domain: str) -> Optional[str]:
    value = read_config(f'pr-{domain}')
    if value:
        return json.loads(value)['token']
    return None


def create_github_pr_tool(domain: str, user: str, repo: str):
    token = _load_access_token(domain)
    if not token:
        # Do the first-time authorization sequence.

        user = input(f'{domain} username: ')
        password = ''
        while not password:
            password = getpass(
                f'{domain} password for {user} (never stored): ')

        auth = _create_access_token(domain, user, password)
        token = auth.token

    # FIXME: Actually use the domain here :)
    gh = github3.login(token=token)
    return GithubPRTool(gh, gh.repository(user, repo))
