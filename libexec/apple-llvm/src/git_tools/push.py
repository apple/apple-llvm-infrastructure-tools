#!/usr/bin/env python3
"""
  Script that implements `git apple-llvm push`.
"""

import os
import sys
import logging
import click
import json
from typing import Optional, List, Dict, Set
from enum import Enum
from contextlib import ExitStack


# Make libexec/apple-llvm/src available for importing.
sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.realpath(os.path.abspath(__file__)))))


from git_tools import git, git_output, get_current_checkout_directory, commit_exists, GitError


# Global log.
log = logging.getLogger(__name__)


# These directories are the monorepo directories that don't belong to the
# monorepo root repo.
all_monorepo_split_dirs = set([
    'clang',
    'clang-tools-extra',
    'compiler-rt',
    'debuginfo-tests',
    'libclc',
    'libcxx',
    'libcxxabi',
    'libunwind',
    'lld',
    'lldb',
    'llgo',
    'llvm',
    'openmp',
    'parallel-libs',
    'polly',
    'pstl',
])

# The name of the ref that should be associated with the source commit in the
# monorepo itself. This association is needed so that the split repo can
# fetch it from the monorepo when importing the monorepo into the split clone.
MONOREPO_SRC_REF_NAME = 'this-branch-shall-be-git-apple-llvm-pushed'


def get_split_directory_for_git_path(git_path: str) -> str:
    """ Returns the split directory for the given git_path, or '-' if the path
        is in the monorepo root repo.
    """
    slash_idx = git_path.find('/')
    dir_prefix = git_path
    if slash_idx != -1:
        dir_prefix = git_path[:slash_idx]
    if dir_prefix in all_monorepo_split_dirs:
        return dir_prefix
    return '-'  # Monorepo root.


def split_dir_to_str(split_dir: str) -> str:
    """
    Return the name that should be printed to the user for the given split dir.
    """
    return 'monorepo root' if split_dir == '-' else split_dir


class GitPushConfiguration:
    """
    Contains the repository mapping needed for git apple-llvm push.

    Attributes:
    branch_to_dest_branch_mapping   The mapping from the [dir/]branch in monorepo
                                    to the destination split repo branch.
    repo_mapping              The mapping between a directory in monorepo to
                              the destination split repo.
    """

    def __init__(self,
                 name: str,
                 branch_to_dest_branch_mapping: Dict[str, str],
                 repo_mapping: Dict[str, str]):
        self.name = name
        self.branch_to_dest_branch_mapping = branch_to_dest_branch_mapping
        self.repo_mapping = repo_mapping

    def get_split_repo_branch(self, dir: str, monorepo_branch: str) -> str:
        """ Returns the appropriate destination branch in the split repo
            for the given monorepo directory and branch.
        """
        combined_key = f'{monorepo_branch}:{dir}'
        if combined_key in self.branch_to_dest_branch_mapping:
            return self.branch_to_dest_branch_mapping[combined_key]
        return self.branch_to_dest_branch_mapping[f'{monorepo_branch}:*']

    def can_push_to_split_dir(self, dir: str) -> bool:
        """ Returns true if this push allows pushes to the specified
            split directory, false otherwise.
        """
        return dir in self.repo_mapping


def has_existing_remote(remote_name: str, remote_url: str, **kwargs):
    try:
        url = git_output('remote', 'get-url', remote_name, **kwargs)
        if url != remote_url:
            git('remote', 'remove', remote_name, **kwargs)
            return False
        return True
    except GitError:
        return False


class SplitRemote:
    """ Controls the operations on the split remote.

        The split remote is cloned into a directory in the monorepo's .git
        directory. Then a local monorepo file path remote is added to it
        allow it to access the monorepo commits.

        The separation ensures that the monorepo doesn't have to fetch a split
        remote from a service like Github as it might be slow because of
        the existing object database in the monorepo.
    """

    def __init__(self, split_dir: str, remote_url: str,
                 destination_branch: str):
        self.split_dir = split_dir
        self.remote_url = remote_url
        self.destination_branch = destination_branch

        # Setup the directory into which the split repo is actually
        # cloned.
        assert os.path.isdir('.git')
        self.remote_clone_dir = os.path.abspath(
            os.path.join('.git', f'apple-llvm-split-{split_dir}.git'))
        if not os.path.isdir(self.remote_clone_dir):
            os.mkdir(self.remote_clone_dir)
            git_output('init', '--bare', git_dir=self.remote_clone_dir)

        self.monorepo_remote_url = os.path.abspath(os.getcwd())
        # The final commit hash that should be pushed.
        self.commit_hash = None

    def update_remote(self):
        if not has_existing_remote('origin', self.remote_url, git_dir=self.remote_clone_dir):
            git('remote', 'add', 'origin', self.remote_url,
                git_dir=self.remote_clone_dir)
        log.debug('fetching the remote for %s', self.split_dir)
        git('fetch', '--no-tags', 'origin', self.destination_branch,
            git_dir=self.remote_clone_dir, stderr=None)

    def update_mono_remote(self):
        if not has_existing_remote('mono', self.monorepo_remote_url,
                                   git_dir=self.remote_clone_dir):
            git('remote', 'add', 'mono', self.monorepo_remote_url,
                git_dir=self.remote_clone_dir)
        git('fetch', 'mono', MONOREPO_SRC_REF_NAME,
            git_dir=self.remote_clone_dir)

    def begin(self):
        os.chdir(self.remote_clone_dir)

    def push(self, dry_run: bool = False):
        click.echo(click.style(
            f'\nPushing to {split_dir_to_str(self.split_dir)}:', bold=True))
        if dry_run:
            click.echo('🛑 dry run, stopping before pushing.')
            return
        git('push', 'origin',
            f'{self.commit_hash}:{self.destination_branch}',
            git_dir=self.remote_clone_dir,
            stderr=None)


def isKnownTrackingBranch(remote: str, b: str) -> bool:
    """ Returns true if the given branch is a known remote branch that we
        should look out for when computing the boundary of the commit graph. """
    remote_prefix = f'{remote}/'
    if not b.startswith(remote_prefix):
        return False
    known_prefixes = set(['llvm', 'apple', 'internal', 'swift'])
    return b[len(remote_prefix):].split('/')[0] in known_prefixes


class CommitGraph:
    """
        Represents a commit graph that should be pushed.

        Attributes:
        commits    The list of commits (HEAD -> TAIL) that we want to push.
        roots      The list of monorepo root commits that we need to remap.
        """

    def __init__(self, commits: List[str], roots: List[str]):
        assert len(commits) > 0
        assert len(roots) > 0
        self.commits = commits
        self.roots = roots

    @property
    def source_commit_hash(self):
        return self.commits[0]

    @property
    def has_merges(self):
        return len(git_output('rev-list', '--min-parents=2',
                              self.source_commit_hash, '--not', *self.roots)) > 0

    def __get_changed_filenames(self) -> str:
        return git_output('log', '--format=', '--name-only',
                          self.source_commit_hash, '--not', *self.roots)

    def compute_changed_files(self, split_dir: Optional[str] = None) -> Set[str]:
        """ Return the set of files modified by the commit graph. """
        if split_dir:
            ls = len(split_dir)
            return set(map(lambda path: path if split_dir == '-' else path[ls + 1:],
                           filter(lambda path: get_split_directory_for_git_path(path) == split_dir,
                                  self.__get_changed_filenames().splitlines())))
        return set(self.__get_changed_filenames().splitlines())

    def compute_changed_split_repos(self) -> List[str]:
        """ Return the list of split repos modified by the commit graph. """
        result = set()
        for filepath in self.__get_changed_filenames().splitlines():
            result.add(get_split_directory_for_git_path(filepath))
        x = list(result)
        x.sort()
        return x


def compute_commit_graph(git_output: str) -> Optional[CommitGraph]:
    """ Parse the output of `git rev-list` and return the `CommitGraph`
        object, or none if the output is invalid/empty.
    """
    rev_list = git_output.splitlines()
    commits = list(filter(lambda rev: not rev.startswith('-'), rev_list))
    roots = list(map(lambda rev: rev[1:], filter(
        lambda rev: rev.startswith('-'), rev_list)))
    if len(commits) < 1 or len(roots) < 1:
        return None
    return CommitGraph(commits, roots)


def find_base_split_commit(split_dir, base_commit) -> Optional[str]:
    """ Return the hash of the base commit in the specified
        split repository derived from the specified monorepo base commit. """
    mono_base_commit = git_output('rev-list', '--first-parent', '-n', '1', '--grep',
                                  f'^apple-llvm-split-dir: {split_dir}/*$', base_commit,
                                  ignore_error=True)
    if not mono_base_commit:
        return None
    SPLIT_COMMIT_TRAILER = 'apple-llvm-split-commit:'
    for line in git_output('rev-list', '-n', '1', '--format=%(trailers:only)',
                           mono_base_commit).splitlines():
        if line.startswith(SPLIT_COMMIT_TRAILER):
            return line[len(SPLIT_COMMIT_TRAILER):].strip()
    return None


class RegraftNoSplitRootError(Exception):
    """ One of the roots in commit graph has no corresponding split root. """

    def __init__(self, root_commit_hash: str):
        self.root_commit_hash = root_commit_hash


class RegraftMissingSplitRootError(Exception):
    """ The split root is not a valid git commit. """

    def __init__(self, root_commit_hash: str):
        self.root_commit_hash = root_commit_hash


dev_null_fd = None


def get_dev_null():
    """Lazily create a /dev/null fd for use in shell()"""
    global dev_null_fd
    if dev_null_fd is None:
        dev_null_fd = open(os.devnull, 'w')
    return dev_null_fd


def regraft_commit_graph_onto_split_repo(commit_graph: CommitGraph,
                                         split_dir: str) -> Optional[CommitGraph]:
    """ This function takes a monorepo commit graph and regrafts it on top of
        the appropriate split repo commit graph. The resulting commit graph
        is returned, or nothing if the rewrite lead to empty commits.

        This is done using the git `filter-branch` command to recreate commits.
        It operates on trees instead of diffs, so the commits have to be
        re-parented appropriately to preserve the intended diff between the two
        trees.

        The roots on which the regraft is performed are found by looking for
        the matching `apple-llvm-split-*` trailers to determine the approriate
        split repo root. Right now regrafting from the upstream LLVM.org monorepo
        roots isn't unsupported (FIXME).

        Raises `RegraftMissingSplitRootError` or `RegraftMissingSplitRootError`
        when the commit graph roots can't be remapped.
    """
    assert split_dir == '-' or split_dir in all_monorepo_split_dirs
    base_split_commits = {}
    for root in commit_graph.roots:
        base_split_commit = find_base_split_commit(split_dir, root)
        if not base_split_commit:
            raise RegraftNoSplitRootError(root)
        if not commit_exists(base_split_commit):
            raise RegraftMissingSplitRootError(root)
        base_split_commits[root] = base_split_commit

    # Construct a filter the rewrites the history to the split directory.
    dir_filter_args: List[str] = []
    if split_dir == '-':
        split_dirs = ' '.join(list(all_monorepo_split_dirs))
        dir_filter_args = ['--index-filter',
                           f'git rm -r --cached --ignore-unmatch {split_dirs}']
    else:
        dir_filter_args = ['--index-filter',
                           f'git read-tree $(git rev-parse $GIT_COMMIT:{split_dir})']
    # Construct a parent filter to replace roots with split base commits.
    parent_filter_cmd = 'cat | sed ' + ' '.join([f'-e s,{mono},{split},'
                                                 for (mono, split)
                                                 in base_split_commits.items()])

    # Setup a work branch that should be rewritten.
    branch_name = f'temp-apple-llvm-push-{split_dir}'
    git('branch', '-f', branch_name, commit_graph.source_commit_hash)

    # Rewrite the branch.
    try:
        # We need the `-f` filter-branch argument to force overwrite of the
        # backup created by git.
        # FIXME: Investigate if we can remove it.
        git('filter-branch', '-f', '--prune-empty',
            '--parent-filter', parent_filter_cmd, *dir_filter_args,
            branch_name, '--not', *commit_graph.roots,
            stdout=get_dev_null())
    except GitError as err:
        # Nothing was rewritten!
        if err.stderr.find('nothing to rewrite') != -1:
            return None
        raise err

    # Compute the updated commit graph.
    rev_list = git_output('rev-list', '--boundary', branch_name,
                          '--not', *[split for (mono, split)
                                     in base_split_commits.items()])
    split_commit_graph = compute_commit_graph(rev_list)
    if split_commit_graph is None:
        return None
    result = CommitGraph(split_commit_graph.commits,
                         split_commit_graph.roots)

    # Verify the integrity of the regraft by checking changed files.
    original_changed_files = commit_graph.compute_changed_files(
        split_dir=split_dir)
    regrafted_changed_files = result.compute_changed_files()
    assert original_changed_files == regrafted_changed_files
    return result


class MergeStrategy(Enum):
    FastForwardOnly = 1
    RebaseOrMerge = 2
    Rebase = 3


class ImpossibleMergeError(Exception):
    """ The error that's reported when merging. """

    def __init__(self, git_error: Optional[GitError]):
        self.git_error = git_error


def merge_commit_graph_with_top_of_branch(commit_graph: CommitGraph,
                                          split_dir: str,
                                          destination_branch: str,
                                          strategy: MergeStrategy) -> str:
    """ Merge/rebase a regrafted split commit graph on top of the
        destination split branch. """
    # The relative path (from the split .git directory) to the temporary
    # working checkout.
    split_worktree_path = f'.git/apple-llvm-push-checkout-{split_dir}'
    branch_name = f'temp-apple-llvm-push-merged-{split_dir}'

    git('worktree', 'remove', '--force', split_worktree_path,
        ignore_error=True, stdout=get_dev_null(), stderr=get_dev_null())
    git('branch', '-f', '-D', branch_name, ignore_error=True,
        stdout=get_dev_null(), stderr=get_dev_null())
    git('worktree', 'add', '-f', '-b', branch_name, split_worktree_path,
        destination_branch)

    with ExitStack() as cleanups:
        # Remove the worktree once we're done.
        cleanups.callback(lambda: git('worktree', 'remove',
                                      '-f', split_worktree_path, ignore_error=True))
        # Try the fast-forward only first.
        try:
            git('merge', '--ff-only', commit_graph.source_commit_hash,
                git_dir=split_worktree_path)
        except GitError as err:
            if strategy == MergeStrategy.FastForwardOnly:
                raise ImpossibleMergeError(err)
        if strategy != MergeStrategy.FastForwardOnly:
            # Try rebasing.
            if not commit_graph.has_merges:
                git('rebase', '--onto', branch_name, branch_name,
                    commit_graph.source_commit_hash, git_dir=split_worktree_path)
            elif strategy == MergeStrategy.Rebase:
                raise ImpossibleMergeError(GitError(['rebase'], 1,
                                                    stdout='',
                                                    stderr='unable to rebase history with merges'))
            else:
                assert strategy == MergeStrategy.RebaseOrMerge
                # Fallback to merge.
                git('merge', commit_graph.source_commit_hash,
                    git_dir=split_worktree_path)
        result = git_output('rev-parse', 'HEAD', git_dir=split_worktree_path)
    return result


def load_push_config(source_ref: str, dest_branch: str) -> Optional[GitPushConfiguration]:
    config_name = dest_branch.replace('/', '-')
    config = git_output(
        'show', f'{source_ref}:apple-llvm-config/push/{config_name}.json', ignore_error=True)
    if not config:
        return None
    value = json.loads(config)
    # FIXME: Validate json.
    return GitPushConfiguration(name=config_name,
                                branch_to_dest_branch_mapping=value['branch_to_dest_branch_mapping'],
                                repo_mapping=value['repo_mapping'])


def fatal(message: str):
    click.echo(click.style('fatal: ', fg='red') + message, err=True)
    sys.exit(1)


MERGE_STRATEGY_OPTION = {
    'ff-only': MergeStrategy.FastForwardOnly,
    'rebase': MergeStrategy.Rebase,
    'rebase-or-merge': MergeStrategy.RebaseOrMerge
}


@click.command()
@click.argument('refspec', metavar='<refspec>')
@click.option('--dry-run', is_flag=True, default=False,
              help='Do not push changes to remotes.')
@click.option('--verbose', is_flag=True, default=False,
              help='Emit verbose output.')
@click.option('--merge-strategy', type=click.Choice([x for x in MERGE_STRATEGY_OPTION]),
              callback=lambda c, p, v: MERGE_STRATEGY_OPTION[v] if v in MERGE_STRATEGY_OPTION else None,
              default='rebase-or-merge',
              help='The strategy to employ for forwarding split repo commits on '
                   'top of the target branch (default: rebase-or-merge).')
@click.option('--push-limit', type=int, default=50,
              help='Prohibit pushing a lot of commits, use 0 for unlimited (default: 50)')
def git_apple_llvm_push(refspec, dry_run, verbose, merge_strategy, push_limit):
    """ Push changes back to the split Git repositories. """
    logging.basicConfig(level=logging.DEBUG if verbose else logging.WARNING,
                        format='%(levelname)s: %(message)s')

    # Verify that we're in a git checkout.
    git_path = get_current_checkout_directory()
    if git_path is None:
        fatal('not a git repository')
    os.chdir(git_path)

    # Figure out the set of remote branches we care about.
    remote = 'origin'
    remote_monorepo_branches = [x.strip() for x in git_output(
        'branch', '-r', '-l').splitlines()]
    remote_monorepo_branches = list(
        filter(lambda x: isKnownTrackingBranch(remote, x), remote_monorepo_branches))
    log.info('Branches we care about %s', remote_monorepo_branches)

    refs = refspec.split(':')
    source_ref = refs[0]
    dest_ref = refs[1]
    remote_dest_ref = f'{remote}/{dest_ref}'
    # Verify that the source ref is valid and get its commit hash.
    source_commit_hash = git_output('rev-parse', source_ref, ignore_error=True)
    if source_commit_hash is None:
        fatal(f'source Git refspec "{source_ref}" is invalid')
    # Ensure that the source ref is associated with a ref that can be fetched.
    git('branch', '-f', MONOREPO_SRC_REF_NAME, source_commit_hash)

    # Verify that the destination ref is valid and load its push config.
    dest_commit_hash = git_output(
        'rev-parse', remote_dest_ref, ignore_error=True)
    if dest_commit_hash is None:
        fatal(f'destination Git refspec "{dest_ref}" is invalid')
    push_config = load_push_config(source_commit_hash, dest_ref)
    if push_config is None:
        fatal(f'destination Git refspec "{dest_ref}" cannot be pushed to.')

    # The rev-list command is used to compute the graph we would like to
    # commit.
    rev_list = git_output('rev-list', '--boundary', source_commit_hash,
                          '--not', *remote_monorepo_branches,
                          ignore_error=True)
    if rev_list is None:
        fatal('unable to determine the commit graph to push')
    commit_graph = compute_commit_graph(rev_list)
    if commit_graph is None:
        print('No commits to commit: everything up-to-date.')
        return
    # Prohibit pushing more than 50 commits by default in a bid to avoid
    # inadvertent mistakes.
    if push_limit != 0 and len(commit_graph.commits) >= push_limit:
        fatal(
            f'pushing {len(commit_graph.commits)} commits, are you really sure?'
            f'\nPass --push-limit={len(commit_graph.commits)+1} if yes.')

    click.echo(click.style(
        f'Preparing to push to {len(commit_graph.commits)} commits:', bold=True))
    git('log', '--format=%h %s', '--graph', commit_graph.source_commit_hash,
        '--not', *commit_graph.roots)

    # FIXME: Verify the commit graph! - commits that in monorepo already.

    # Prepare the split remotes.
    split_repos_of_interest = commit_graph.compute_changed_split_repos()

    click.echo(
        f'Split repos that should be updates: {", ".join(map(split_dir_to_str, split_repos_of_interest))}\n')

    split_remotes = {}
    for split_dir in split_repos_of_interest:
        if not push_config.can_push_to_split_dir(split_dir):
            fatal(
                f'push configuration "{push_config.name}" prohibits pushing to "{split_dir}"')
        remote = SplitRemote(split_dir, push_config.repo_mapping[split_dir],
                             push_config.get_split_repo_branch(split_dir,
                                                               dest_ref))
        click.echo(click.style(
            f'Fetching "{remote.destination_branch}" for {split_dir_to_str(split_dir)}...', bold=True))
        try:
            remote.update_remote()
        except GitError:
            fatal(
                f'failed to fetch from the remote for {split_dir_to_str(split_dir)}.')
        click.echo(
            'Fetching monorepo commits from monorepo to the split clone (takes time on first push)...\n')
        remote.update_mono_remote()
        split_remotes[split_dir] = remote

    # Regraft the commit history.
    for split_dir in split_repos_of_interest:
        click.echo(click.style(
            f'Regrafting the commits from monorepo to {split_dir_to_str(split_dir)}...', bold=True))
        split_remotes[split_dir].begin()
        split_remotes[split_dir].regrafted_graph = regraft_commit_graph_onto_split_repo(commit_graph,
                                                                                        split_dir)

    # Merge/rebase the commit history.
    for split_dir in split_repos_of_interest:
        click.echo(click.style(
            f'\nRebasing/merging the {split_dir_to_str(split_dir)} commits...', bold=True))
        remote = split_remotes[split_dir]
        remote.begin()
        try:
            remote.commit_hash = merge_commit_graph_with_top_of_branch(remote.regrafted_graph,
                                                                       split_dir,
                                                                       'origin/' + remote.destination_branch,
                                                                       merge_strategy)
        except ImpossibleMergeError:
            kind = 'fast forward' if merge_strategy == MergeStrategy.FastForwardOnly else 'merge'
            fatal(
                f'unable to {kind} commits in {split_dir}. Please rebase your monorepo commits first.')

    # Once everything is ready, push!
    for split_dir in split_repos_of_interest:
        split_remotes[split_dir].push(dry_run)


if __name__ == "__main__":
    git_apple_llvm_push()
