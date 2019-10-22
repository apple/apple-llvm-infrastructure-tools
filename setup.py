#!/usr/bin/env python3

import os
import sys
import pkgutil
from shutil import rmtree

from setuptools import find_packages, setup, Command
from setuptools.command.install import install

here = os.path.abspath(os.path.dirname(__file__))

# The source packages are now located in libexec apple-llvm.
packages = find_packages('git_apple_llvm', exclude=[
                         "tests", "*.tests", "*.tests.*", "tests.*"])
packages = [f'git_apple_llvm.{x}' for x in packages]


class CleanCommand(Command):
    """Custom clean command to tidy up the project root."""
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        print('Removing build/ dist/ and egg-info/')
        rmtree('build', ignore_errors=True)
        rmtree('dist', ignore_errors=True)
        rmtree('git_apple_llvm.egg-info', ignore_errors=True)


class InfoCommand(Command):
    """The command that prints out the resolved information, like packages."""
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        print('Packages for git-apple-llvm:')
        for package in packages:
            print('  ', package)


# FIXME: mention license.
setup(
    name='git-apple-llvm',
    version='0.1.0',
    description='Infrastructure-related tools for working with LLVM-derived projects',
    author='Apple',
    python_requires='>=3.7.0',
    packages=packages,
    install_requires=['click', 'appdirs', 'github3.py', 'redis'],
    cmdclass={
        'info': InfoCommand,
        'clean': CleanCommand,
    }
)
