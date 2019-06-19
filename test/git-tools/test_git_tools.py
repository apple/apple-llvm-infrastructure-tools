"""
  Tests for the git tools.
"""

import os
import pytest
from git_tools import git, git_output, get_current_checkout_directory, commit_exists, GitError


def test_git_invocation(tmp_path):
    """ Tests for the git/git_output functions. """
    repo_path = tmp_path / 'repo'
    repo_path.mkdir()
    assert repo_path.is_dir()  # Ensure the dir is there for us to work with.
    repo_dir = str(repo_path)

    git('init', git_dir=repo_dir)

    (repo_path / 'initial').write_text(u'initial')
    git('add', 'initial', git_dir=repo_dir)

    # Check that we can report an error on failure.
    with pytest.raises(GitError) as err:
        git('add', 'foo', git_dir=repo_dir)
    assert err.value.stderr.startswith('fatal')
    assert repr(err.value).startswith('GitError')

    # Check that errors can be ignored.
    git('add', 'foo', git_dir=repo_dir, ignore_error=True)

    output = git_output('commit', '-m', 'initial', git_dir=repo_dir)
    assert len(output) > 0

    # Ensure that the output is stripped.
    output = git_output('rev-list', 'HEAD', git_dir=repo_dir)
    assert '\n' not in output
    output = git_output('rev-list', 'HEAD', git_dir=repo_dir, strip=False)
    assert '\n' in output

    # Ensure that commit exists works only for commit hashes.
    hash = output.strip()
    assert commit_exists(hash)
    assert not commit_exists('HEAD')
    assert not commit_exists(hash + 'abc')
    assert not commit_exists('000000')

    # Ensure that we can get the directory of the checkout even when the
    # working directory is a subdirectory.
    os.chdir(repo_dir)
    dir_a = get_current_checkout_directory()
    (repo_path / 'subdir').mkdir()
    cwd = os.getcwd()
    os.chdir(os.path.join(repo_dir, 'subdir'))
    dir_b = get_current_checkout_directory()
    os.chdir(cwd)
    assert dir_a == dir_b
