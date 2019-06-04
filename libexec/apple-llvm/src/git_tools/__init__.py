"""
  Utilities for Git support.
"""

from typing import Optional, List
import subprocess
import logging
import shlex

log = logging.getLogger(__name__)


class GitError(Exception):
    """
    An exception thrown if the git command failed.

    Attributes
    ----------
    args : List[str]
    The list of arguments passed to `git`.
    returncode : int
    The exit code of the `git` process.
    stdout : str
    The output of `git`.
    stderr : str
    The error output of `git`.
    """

    def __init__(self, args, returncode: int, stdout: str, stderr: str):
        self.args = args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def __repr__(self):
        return f'GitError({self.args}, {self.returncode}, "{self.stdout}", "{self.stderr}")'


def _git_to_str(args: List[str]):
    return 'git ' + ' '.join(map(lambda arg: shlex.quote(arg), args))


def invoke(*cmd, git_dir: Optional[str] = None,
           stdin: Optional[str] = None,
           capture_stdout: bool = False, strip: bool = True, ignore_error: bool = False,
           timeout: Optional[int] = None):
    """ Invokes a git subprocess with the passed string arguments and return
        the stdout of the git command as a string if text otherwise a file
        handle.
    """
    if git_dir is not None:
        all_args = ['-C', git_dir] + list(cmd)
    else:
        all_args = list(cmd)
    log.debug('$ %s', _git_to_str(all_args))
    p = subprocess.Popen(['git'] + all_args,
                         stdout=subprocess.PIPE if capture_stdout else None,
                         stderr=subprocess.PIPE,
                         stdin=subprocess.PIPE if stdin else None,
                         universal_newlines=True)
    stdout, stderr = p.communicate(input=stdin, timeout=timeout)
    if p.returncode == 0:
        if stdout:
            if strip:
                stdout = stdout.rstrip()
            for line in stdout.splitlines():
                log.debug('STDOUT: %s', line)
        if stderr:
            for line in stderr.rstrip().splitlines():
                log.warning('STDERR: %s', line)
        return stdout
    log.debug('EXIT STATUS: %d', p.returncode)
    if stderr:
        for line in stderr.rstrip().splitlines():
            log.debug('STDERR: %s', line)
    if ignore_error:
        return None
    raise GitError(all_args, p.returncode, stdout, stderr)


def git(*cmd, **kwargs):
    """ Invokes a git subprocess with the passed string arguments. """
    return invoke(*cmd, **kwargs)


def git_output(*cmd, **kwargs):
    """ Invokes a git subprocess with the passed string arguments and return
        the stdout of the git command.
    """
    return invoke(*cmd, **kwargs, capture_stdout=True)


def get_current_checkout_directory() -> Optional[str]:
    """ Return the path to the current git checkout, or None otherwise. """
    return git_output('rev-parse', '--show-toplevel', ignore_error=True)
