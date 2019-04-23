#!/usr/bin/env python3
"""
   Script that automatically updates apple-llvm-infrastructure-tools once a day.
"""

from datetime import datetime
import os
import subprocess
from typing import List
import sys
import logging


class InfrastructureToolsNotGitRepoError(Exception):
    """ An error thrown whenever the apple-llvm tools are not located in a .git checkout  """


class InfrastructureToolsNotMasterCheckout(Exception):
    """ An error thrown whenever the apple-llvm tools checkout isn't apple/master """


class InfrastructureToolsGitRepo:
    """ A class that represents the apple-llvm tools .git checkout """

    def __init__(self):
        currentdir = os.path.dirname(os.path.abspath(__file__))
        self.checkout_path = os.path.dirname(os.path.dirname(currentdir))
        self.git_path = os.path.join(self.checkout_path, ".git")
        if not os.path.isdir(self.git_path):
            raise InfrastructureToolsNotGitRepoError

    def run_git_command(self, args: List[str]):
        """ Runs a git command on the repository """
        subprocess.check_call(['git', '-C', self.checkout_path] + args)

    def get_remote_name_if_master_checkout(self):
        """
            Returns the name of the remote that the checkout is pulled from, if
            that checkout is on a master branch.

            If the checkout has no tracking branch, or if the checkout is on non-master
            branch, the InfrastructureToolsNotMasterCheckout error is raised.
        """
        try:
            remote_branch = subprocess.check_output(['git', '-C', self.checkout_path, 'rev-parse', '--abbrev-ref',
                                                     '--symbolic-full-name', '@{u}'], stderr=subprocess.PIPE).decode('utf-8').strip()
            # If we have a checkout of *master, we can probably pull.
            if remote_branch.endswith('/master'):
                slash_idx = remote_branch.find('/')
                if slash_idx != -1:
                    remote = remote_branch[:slash_idx]
                    return remote
        except subprocess.CalledProcessError:
            # The invocation will fail if you have a checkout of a
            # branch without a tracking branch.
            pass
        raise InfrastructureToolsNotMasterCheckout


def _current_day() -> str:
    """ Returns the current year, month and day """
    return datetime.now().strftime('%Y-%m-%d')


def reset_update_state():
    """ Removes all update markers from the checkout """
    try:
        repo = InfrastructureToolsGitRepo()
        from shutil import rmtree
        rmtree(os.path.join(repo.git_path, 'update-checking-marker'))
    except InfrastructureToolsNotGitRepoError:
        pass


def update_repository() -> bool:
    """
      This function checks if apple-llvm-infrastructure-tools needs to be updated.
      The checks happen on a daily interval.
      If the repo needs to be updated, it is pulled from the remote.
      Returns true if the repo was updated, false otherwise.
    """
    try:
        repo = InfrastructureToolsGitRepo()
        remote_name = repo.get_remote_name_if_master_checkout()
        assert remote_name.find('/') == -1
        # The update marker will store the last update date
        store_dir = os.path.join(
            repo.git_path, 'update-checking-marker', remote_name)
        now = _current_day()
        store_path = os.path.join(store_dir, now)
        if os.path.exists(store_path):
            # We've already checked for updates!
            return False
        logging.debug('update marker not found; will pull from remote')
        # Write out the update marker
        os.makedirs(store_dir, exist_ok=True)  # Python >= 3.2
        file = open(store_path, 'w')
        file.write('.')
        file.close()
        # And update the repository.
        print('checking for `git apple-llvm` updates from `{}`...'.format(remote_name))
        # FIXME: What if the rebase failed? Should we abort it?
        repo.run_git_command(['pull', '--ff-only', remote_name])
        # FIXME: Return true when tree hash changed only.
        # FIXME: Garbage-collect old update markers periodically.
        return True
    except InfrastructureToolsNotGitRepoError:
        logging.warning(
            'using `git apple-llvm` without source; upstream updates are ignored.')
        return False
    except InfrastructureToolsNotMasterCheckout:
        logging.warning(
            'using custom `git apple-llvm`; upstream updates are ignored.')
        return False


if __name__ == "__main__":
    # FIXME: Need to rerun the makefile once update completes.
    update_repository()
