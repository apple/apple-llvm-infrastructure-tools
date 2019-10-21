"""For now lets store CI results in Redis.

If we need something more complex, we can sort it out later.

"""
import os
import sys

import redis
import logging

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
