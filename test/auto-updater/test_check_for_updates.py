"""
  Tests for the update checker.
"""

import os
import sys
import subprocess
from shutil import copy, rmtree

from git import Repo


def init_repo(tmp_path):
    d = tmp_path / 'fake_tools'
    d.mkdir()
    p = d / 'initial'
    p.write_text(u'initial')
    filepath = str(d.resolve())
    repo = Repo.init(filepath)
    repo.index.add(['initial'])
    repo.index.commit('initial commit')
    return repo


def test_update_checker(tmp_path):
    currentdir = os.path.dirname(os.path.abspath(__file__))
    rootdir = os.path.dirname(os.path.dirname(currentdir))

    # Create a fake tools repo with 1 commit,
    # and a clone repo.
    fake_repo = init_repo(tmp_path)
    clone_dir = tmp_path / 'clone_repo'
    clone_dir.mkdir()
    clone_repo = Repo.clone_from(
        str(tmp_path / 'fake_tools'), str(clone_dir.resolve()))

    # Copy tools/check_for_updates and utils/* over to the clone
    clone_dir_src = clone_dir / 'src'
    clone_dir_src.mkdir()
    clone_dir_tools = clone_dir_src / 'tools'
    clone_dir_tools.mkdir()
    copy(os.path.join(rootdir, 'src', 'tools', 'check_for_updates.py'),
         str((clone_dir_tools / 'check_for_updates.py').resolve()))

    # Import the fake tools/check_for_updates
    sys.path.insert(0, str((clone_dir / 'src').resolve()))
    from tools.check_for_updates import update_repository, reset_update_state

    # Check for updates
    print(clone_dir)
    assert update_repository() == True
    assert update_repository() == False
    reset_update_state()
    assert update_repository() == True

    # Update checking should work with a modified 'master'
    reset_update_state()
    p = clone_dir / 'second_file'
    p.write_text(u'second_file')
    clone_repo.index.add(['second_file'])
    clone_repo.index.commit('modified clone')
    assert update_repository() == True
    assert update_repository() == False

    # Update checking should not work with another branch
    reset_update_state()
    clone_repo.git.checkout('HEAD', b='my_branch')
    p = clone_dir / 'my_branch'
    p.write_text(u'my_branch')
    clone_repo.index.add(['my_branch'])
    clone_repo.index.commit('my_branch')
    assert update_repository() == False

    # Update checking should not work without a git checkout
    clone_repo.git.checkout('master')
    assert update_repository() == True
    reset_update_state()
    rmtree(str((clone_dir / '.git').resolve()))
    assert update_repository() == False
