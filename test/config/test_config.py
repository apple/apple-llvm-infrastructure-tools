"""
  Tests for the config files.
"""

import os
import pytest
from git_apple_llvm.config import read_config, write_config


@pytest.fixture(scope='function')
def config_dir(tmp_path):
    dir = str(tmp_path / 'git-apple-llvm')
    os.environ['GIT_APPLE_LLVM_CONFIG_DIR'] = dir
    yield dir
    del os.environ['GIT_APPLE_LLVM_CONFIG_DIR']


def test_config_file(config_dir):
    write_config('config', 'test')
    assert os.path.isfile(os.path.join(config_dir, 'config'))
    assert read_config('config') == 'test'
    assert read_config('none') is None
