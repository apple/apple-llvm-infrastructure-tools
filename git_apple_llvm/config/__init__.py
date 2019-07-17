"""
  Module for working with local configuration files.
"""

from appdirs import user_config_dir
import os
import logging
from typing import Optional


# Global log.
log = logging.getLogger(__name__)


def get_config_dir() -> str:
    cd = os.getenv('GIT_APPLE_LLVM_CONFIG_DIR')
    if cd:
        return cd
    return user_config_dir('git-apple-llvm', 'Apple')


def get_or_create_config_dir() -> str:
    dir = get_config_dir()
    os.makedirs(dir, exist_ok=True)
    return dir


def write_config(filename: str, contents: str):
    """
      Write out a git-apple-llvm configuration file to the specified filename
      at the appropriate location.
    """
    path = os.path.join(get_or_create_config_dir(), filename)
    log.debug('Writing %s configuration to %s', filename, path)
    with open(path, 'w') as f:
        f.write(contents)
    # Set the permissions for the token only to this user.
    os.chmod(path, 0o600)


def read_config(filename: str) -> Optional[str]:
    """
      Read a git-apple-llvm configuration file from the specified filename
      at the appropriate location.

      Returns the contents of the config or None.
    """
    path = os.path.join(get_config_dir(), filename)
    try:
        with open(path, 'r') as f:
            result = f.read()
    except FileNotFoundError:
        return None
    log.debug('Loaded %s configuration from %s', filename, path)
    return result
