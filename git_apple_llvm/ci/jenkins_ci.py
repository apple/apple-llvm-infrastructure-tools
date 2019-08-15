from typing import Dict, List, Optional, Tuple
import requests
import logging
import urllib.parse
from getpass import getuser, getpass
from socket import gethostname
from git_apple_llvm.config import read_config, write_config
import sys
import json

log = logging.getLogger(__name__)


class CIDispatchError(Exception):
    """
    An exception thrown if the CI dispatching failed.

    Attributes
    ----------
    url : str
    The url that was dispatched for.
    status_code : int
    The HTTP code returned by the URL request.
    error : str
    The textual error.
    """

    def __init__(self, url: str, status_code: int, error: str):
        self.url = url
        self.status_code = status_code
        self.error = error

    def __repr__(self):
        return f'CIDispatchError("{self.url}", {self.status_code}, "{self.error}")'


class JenkinsCIJob:
    """
    Represents a job that can be dispatched using Jenkins CI

    e.g. JSON representation:
    {"name": "a-RA", "url": "https://...", "params": { "build_variant": "a" }}

    Attributes
    ----------
    name : str
    The name of the CI job
    url : str
    The URL to the jenkins job
    params : Dict[str, str]
    The parameters to pass to the job
    """

    def __init__(self, json: Dict):
        self.name: str = json['name']
        self.url: str = json['url']
        self.params: Dict[str, str] = json['params']

    def dispatch(self, params: Dict[str, str], auth, test_plan_name: str):
        """ Dispatches the request to the CI job """
        url: str = self.url
        cause: str = urllib.parse.quote(
            f'started by {auth[0]} using git apple-llvm')
        url += f'/buildWithParameters?token=GIT_APPLE_LLVM&cause={cause}'
        params.update(self.params)
        for (key, value) in params.items():
            url += f'&{key}={value}'
        log.info('Performing jenkins request "%s"', url)
        ret = requests.post(url, auth=auth)
        if ret.status_code != 201:
            raise CIDispatchError(url, ret.status_code, ret.text)
        description: str = ''
        if 'pullRequestID' in params:
            description = f'PR #{params["pullRequestID"]}'
        print(f'✅ requested {test_plan_name} [{self.name}] ci job for {description}')


def _create_jenkins_token_auth(url: str, domain_key: str, username: str, password: str):
    """
        Creates and saves the jenkins access token.
    """
    # Use a nice name for the access token so it's clear from which
    # machine this token is used. e.g.
    # git apple-llvm for user@my-macbook.local
    token_name = f'git apple-llvm for {getuser()}@{gethostname()}'
    ret = requests.post(f'{url}/me/descriptorByName/jenkins.security.ApiTokenProperty/generateNewToken',
                        data=f'newTokenName={urllib.parse.quote(token_name)}',
                        headers={
                            'content-type': 'application/x-www-form-urlencoded; charset=UTF-8'},
                        auth=(username, password))
    if ret.status_code != 200:
        raise CIDispatchError(
            ret.url, ret.status_code, 'failed to construct a jenkins api token:' + ret.text)
    result = ret.json()
    log.debug('jenkins API token: %s', result)
    if result['status'] != 'ok':
        raise CIDispatchError(
            ret.url, ret.status_code, 'failed to construct a jenkins api token:' + ret.text)
    token_value = result['data']['tokenValue']

    # Save the access token.
    write_config(f'jenkins-{domain_key}',
                 json.dumps({'username': username, 'token': token_value}))
    return (username, token_value)


def _load_jenkins_token_auth(domain_key: str) -> Optional[Tuple[str, str]]:
    value = read_config(f'jenkins-{domain_key}')
    if value:
        j = json.loads(value)
        return (j['username'], j['token'])
    return None


def _get_jenkins_auth(url: str):
    parsed_url = urllib.parse.urlparse(url)
    domain_key: str = parsed_url.netloc + parsed_url.path.replace('/', '-')

    auth = _load_jenkins_token_auth(domain_key)
    if auth:
        return auth

    # Do the first-time authorization sequence.
    print(f'Please provide your "{url}" Jenkins credentials.')
    print(f'They will be used once to create a Jenkins access token that will be saved on this machine.')
    user = input(f'  {url} username: ')
    password: str = ''
    while not password:
        password = getpass(
            f'  {url} password for {user} (never stored): ')
    try:
        return _create_jenkins_token_auth(url, domain_key, user, password)
    except CIDispatchError as err:
        if err.status_code == 401:
            print('error: http error code 401: invalid username/password?')
            sys.exit(1)
        raise


class JenkinsCIConfig:
    """
    Represents a configuration for a Jenkins CI.

    e.g.

    "type": "jenkins",
    "url": "https://...",
    "jobs":
    [
    {"name": "a-RA", "url": "https://...", "params": { "build_variant": "a" }},
    {"name": "b-RA", "url": "https://...", "params": { "build_variant": "b" }}
    ]

    Attributes
    ----------
    jobs : List[JenkinsCIJob]
    the list of jobs that should be dispatched.
    """

    def __init__(self, json: Dict):
        if json['type'] != 'jenkins':
            raise RuntimeError('not a jenkins CI config')
        jobs: List[Dict] = json['jobs']
        if len(jobs) < 1:
            raise RuntimeError('missing jobs in CI config')
        self.url = json['url']
        self.jobs = [JenkinsCIJob(x) for x in jobs]

    def dispatch_with_auth(self, params: Dict[str, str], auth, test_plan_name: str):
        for job in self.jobs:
            job.dispatch(params, auth, test_plan_name)

    def dispatch(self, params: Dict[str, str], test_plan_name: str):
        """ Queries for auth parameters and dispatches the CI jobs """
        auth = _get_jenkins_auth(self.url)
        self.dispatch_with_auth(params, auth, test_plan_name)
