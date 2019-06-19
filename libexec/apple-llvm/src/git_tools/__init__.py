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
           stdout=None,
           stderr=subprocess.PIPE,
           strip: bool = True, ignore_error: bool = False,
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
                         stdout=stdout,
                         stderr=stderr,
                         stdin=subprocess.PIPE if stdin else None,
                         universal_newlines=True)
    out, err = p.communicate(input=stdin, timeout=timeout)
    if p.returncode == 0:
        if out:
            if strip:
                out = out.rstrip()
            for line in out.splitlines():
                log.debug('STDOUT: %s', line)
        if err:
            for line in err.rstrip().splitlines():
                log.debug('STDERR: %s', line)
        return out
    log.debug('EXIT STATUS: %d', p.returncode)
    if err:
        for line in err.rstrip().splitlines():
            log.debug('STDERR: %s', line)
    if ignore_error:
        return None
    raise GitError(all_args, p.returncode, out, err)


def git(*cmd, **kwargs):
    """ Invokes a git subprocess with the passed string arguments. """
    return invoke(*cmd, **kwargs)


def git_output(*cmd, **kwargs):
    """ Invokes a git subprocess with the passed string arguments and return
        the stdout of the git command.
    """
    return invoke(*cmd, **kwargs, stdout=subprocess.PIPE)


def get_current_checkout_directory() -> Optional[str]:
    """ Return the path to the current git checkout, or None otherwise. """
    return git_output('rev-parse', '--show-toplevel', ignore_error=True)


def commit_exists(hash: str) -> bool:
    """ Return true if the specified commit exists """
    result = git_output('rev-parse', hash, ignore_error=True)
    return result and result == hash
