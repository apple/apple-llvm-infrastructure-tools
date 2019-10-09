""" A module that verifies if a branch contents fits within imposed restrictions """

from git_apple_llvm.git_tools import get_current_checkout_directory
import click
import sys
import logging
import os


def fatal(message: str):
    click.echo(click.style('fatal: ', fg='red') + message, err=True)
    sys.exit(1)


def verify_swift_branch(branch_name: str):
    """
    Verify the rules for swift/ branches.
    1: A swift branch should have apple-llvm-config/am/<branch-name>.json
    2: The primary upstream branch of the swift/ branch should have identical contents, except for LLDB.
    """
    assert branch_name.startswith('swift/')
    # FIXME: To implement.


@click.command()
@click.option('--branch-name', required=True,
              help='Branch to verify')
@click.option('--verbose', is_flag=True, default=False,
              help='Emit verbose output')
def verify_branch_contents(branch_name: str, verbose):
    logging.basicConfig(level=logging.DEBUG if verbose else logging.WARNING,
                        format='%(levelname)s: %(message)s [%(filename)s:%(lineno)d at %(asctime)s] ',)

    # Verify that we're in a git checkout.
    git_path = get_current_checkout_directory()
    if git_path is None:
        fatal('not a git repository')
    os.chdir(git_path)

    if branch_name.startswith('swift/'):
        verify_swift_branch(branch_name)


if __name__ == '__main__':
    verify_branch_contents()
