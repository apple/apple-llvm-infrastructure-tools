import click
import logging
from typing import Optional, List
from git_apple_llvm.git_tools import git
from git_apple_llvm.am.core import CommitStates
from git_apple_llvm.am.am_status import print_status
from git_apple_llvm.am.oracle import set_state, get_state, set_build_url, get_build_url

log = logging.getLogger(__name__)

is_verbose = False


@click.group()
@click.option('-v', '--verbose', count=True)
def am(verbose):
    global is_verbose

    # Setup logging. Use verbose flag to determine console output, and log to a file in at debug level.
    is_verbose = bool(verbose)
    level = logging.WARNING - (verbose * 10)
    if level < 1:
        raise ValueError("Too verbose.")
    logger = logging.getLogger()
    logger.setLevel(logging.DEBUG)
    # create file handler which logs even debug messages
    fh = logging.FileHandler('am.log')
    fh.setLevel(logging.DEBUG)
    # create console handler with a higher log level
    ch = logging.StreamHandler()
    ch.setLevel(level)
    # create formatter and add it to the handlers
    fh_formatter = logging.Formatter('%(asctime)s %(levelname)s: %(message)s [%(filename)s:%(lineno)d]')
    fh.setFormatter(fh_formatter)
    ch_fomatter = logging.Formatter('%(levelname)s: %(message)s [%(filename)s:%(lineno)d] ')
    ch.setFormatter(ch_fomatter)
    # add the handlers to the logger
    logger.addHandler(fh)
    logger.addHandler(ch)


@am.command()
@click.option('--target', metavar='<branch>', type=str,
              default=None,
              help='The target branch for which the status should be reported. All branches are shown by default.')
@click.option('--all-commits', is_flag=True, default=False,
              help='List all outstanding commits in the merge backlog.')
@click.option('--remote', metavar='<remote>',
              multiple=True,
              help='The remote(s).')
@click.option('--no-fetch', is_flag=True, default=False,
              help='Do not fetch remote (WARNING: status will be stale!).')
@click.option('--ci-status', is_flag=True, default=False,
              help='Query additional CI status from Redis.')
@click.option('--graph', is_flag=True, default=False,
              help='Generate the automerger graph.')
@click.option('--graph-format', metavar='<format>', type=str,
              default=None,
              help='The file format for the generated graph. Passing this argument implies passing --graph.')
def status(target: Optional[str], all_commits: bool, remote: List[str],
           no_fetch: bool, ci_status: bool, graph: bool, graph_format: str):
    remotes = remote if remote else ['origin']
    if not no_fetch:
        for r in remotes:
            click.echo(
                f'❕ Fetching "{r}" to provide the latest status...')
            git('fetch', r, stderr=None)
            click.echo('✅ Fetch succeeded!\n')
    if graph and not graph_format:
        graph_format = 'pdf'
    print_status(remotes=remotes,
                 target_branch=target,
                 list_commits=all_commits,
                 query_ci_status=ci_status,
                 graph_format=graph_format)


@am.group()
def result():
    """ Set and Get merge results.
    """
    pass


@result.command()
@click.argument('merge_id', envvar='MERGE_ID')
@click.argument('status')
def set(merge_id, status):
    """Set the merge status for a merge ID."""
    if status not in CommitStates.all:
        all_status = ', '.join(CommitStates.all)
        raise click.BadArgumentUsage(f"Status must be one of {all_status}.")
    set_state(merge_id, status)
    click.echo(f"Set {merge_id} to {status}")


@result.command()
@click.argument('merge_id', envvar='MERGE_ID')
def get(merge_id):
    """Get the merge status of a merge_id.
    """
    click.echo(get_state(merge_id))


@am.group()
def url():
    """ Set and Get build URL.
    """
    pass


@url.command()
@click.argument('merge_id', envvar='MERGE_ID')
@click.argument('build_url')
def seturl(merge_id, build_url):
    """Set the build URL for a merge ID."""
    set_build_url(merge_id, build_url)
    click.echo(f"Set build URL for {merge_id} to {build_url}")


@url.command()
@click.argument('merge_id', envvar='MERGE_ID')
def geturl(merge_id):
    """Get the build URL for a merge_id.
    """
    click.echo(get_build_url(merge_id))


if __name__ == '__main__':
    am()
