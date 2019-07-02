#!/usr/bin/env python3

import os
import sys
import pkgutil
from shutil import rmtree

from setuptools import find_packages, setup, Command
from setuptools.command.install import install

here = os.path.abspath(os.path.dirname(__file__))

# The main script is installed into bin.
MAIN_SCRIPT = 'bin/git-apple-llvm'
LIBEXEC_DIR = 'libexec/apple-llvm'

# The bash scripts and compiled dependencies are installed into libexec.
# FIXME: Build the C++ binaries and install them as well.
libexec_data = []
data_dirs = set([LIBEXEC_DIR, os.path.join(LIBEXEC_DIR, 'helpers')])
for r, _, files in os.walk('libexec/apple-llvm'):
    if r not in data_dirs:
        continue
    libexec_data.append((r, [os.path.join(r, f) for f in files]))

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
        print('Files for git-apple-llvm:')
        for data in libexec_data:
            print('  ', data[0])
            for file in data[1]:
                print('    ', file)
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
    scripts=[
        MAIN_SCRIPT
    ],
    data_files=libexec_data,
    install_requires=['click'],
    cmdclass={
        'info': InfoCommand,
        'clean': CleanCommand,
    }
)
