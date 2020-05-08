"""For now lets store CI results in Redis.

If we need something more complex, we can sort it out later.

"""
import os
import sys

import redis
import logging
from typing import Optional
from git_apple_llvm.am.core import CommitStates

log = logging.getLogger(__name__)


def get_credentials(redis_hostname: str):
    """Get the credentials for loggin into redis.

    First try the environ, if they don't exist.
    """

    environment_credentials = os.environ.get('REDIS_PASSWORD')
    if not environment_credentials:
        log.error("No credentials set for access redis in build oracle.")
        sys.exit(1)
    credentials = environment_credentials
    return credentials


host = os.environ.get('REDIS_HOST', '<unknown>')
redis_port = int(os.environ.get('REDIS_PORT', '6379'))
redis_db = int(os.environ.get('REDIS_DB', '8'))


def get_state(tree_hash):
    r = redis.Redis(host=host, port=redis_port, db=redis_db, password=get_credentials(host))
    state = r.get(tree_hash)
    if state:
        log.info(f"State: {state.decode('utf-8')}")
        return state.decode("utf-8")
    return


def set_state(tree_hash, state):
    r = redis.Redis(host=host, port=redis_port, db=redis_db, password=get_credentials(host))
    return r.set(tree_hash, state)


def clear_state(tree_hash):
    r = redis.Redis(host=host, port=redis_port, db=redis_db, password=get_credentials(host))
    return r.delete(tree_hash)


def get_ci_status(commit_hash: str, target_branch: str) -> Optional[str]:
    id = f'{commit_hash}_{target_branch}'
    val = get_state(id)
    if val:
        assert val in CommitStates.all
        return val
    return None


def set_build_url(merge_id: str, url: str) -> bool:
    key = f'{merge_id}.build_url'
    return set_state(key, url)


def get_build_url(merge_id: str) -> Optional[str]:
    key = f'{merge_id}.build_url'
    val = get_state(key)
    if val:
        return val
    return None
