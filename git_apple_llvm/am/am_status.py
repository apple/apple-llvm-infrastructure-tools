"""
  Module for computing the automerger status.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import find_inflight_merges, compute_unmerged_commits
from git_apple_llvm.am.oracle import get_ci_status
from git_apple_llvm.am.zippered_merge import compute_zippered_merges
from git_apple_llvm.am.graph import create_graph, add_branches, compute_zippered_edges, compute_edge
import logging
import click
from typing import Dict, List, Set, Optional


log = logging.getLogger(__name__)


def compute_inflight_commit_count(commits: List[str], commits_inflight: Set[str]) -> int:
    # Scan until we find the first in-flight commit.
    not_seen = 0
    for commit in commits:
        if commit.split(' ')[0] in commits_inflight:
            return len(commits) - not_seen
        not_seen += 1
    return 0


def print_edge_status(upstream_branch: str, target_branch: str,
                      inflight_merges: Dict[str, List[str]], list_commits: bool = False, remote: str = 'origin',
                      graph=None, query_ci_status: bool = False):
    commits_inflight: Set[str] = set(
        inflight_merges[target_branch] if target_branch in inflight_merges else [])

    click.echo(click.style(
               f'[{upstream_branch} -> {target_branch}]', bold=True))
    commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=target_branch,
                                                            upstream_branch=upstream_branch, format='%H %cd')
    if graph:
        edge = compute_edge(upstream_branch, target_branch, commits_inflight, commits, remote, query_ci_status)
        edge.materialize(graph)

    if not commits:
        print(f'- 0 unmerged commits. {target_branch} is up to date.')
        return

    inflight_count = compute_inflight_commit_count(commits, commits_inflight)
    str2 = f'{inflight_count} commits are currently being merged/build/tested.'
    print(f'- {len(commits)} unmerged commits. {str2}')
    print('- Unmerged commits:')

    def print_commit_status(commit: str):
        hash = commit.split(' ')[0]
        status: str = ''
        if query_ci_status:
            ci_state: Optional[str] = get_ci_status(hash, target_branch)
            if ci_state:
                status = ci_state
        if not status:
            if hash in commits_inflight:
                status = 'Auto merge in progress'

        commit_status_line = f'  * {commit}'
        if status:
            commit_status_line += f' : {status}'
        print(commit_status_line)

    print_commit_status(commits[0])
    if list_commits:
        for commit in commits[1:]:
            print_commit_status(commit)
        return
    # Show an abbreviated list of commits.
    if len(commits) > 2:
        print(f'    ... {len(commits) - 2} commits in-between ...')
    if len(commits) > 1:
        print_commit_status(commits[-1])


def print_zippered_edge_status(config: AMTargetBranchConfig, remote: str, graph=None):
    click.echo(click.style(
               f'[{config.upstream} -> {config.target} <- {config.secondary_upstream}]',
               bold=True))
    print('- This is a zippered merge branch!')

    merges: Optional[List[List[str]]] = []
    merges = compute_zippered_merges(remote=remote, target=config.target,
                                     left_upstream=config.upstream,
                                     right_upstream=config.secondary_upstream,
                                     common_ancestor=config.common_ancestor,
                                     stop_on_first=True)

    if graph:
        # FIXME: Don't duplicate compute_unmerged_commits in compute_zippered_edges.
        left_edge, right_edge = compute_zippered_edges(config, remote, merges)
        left_edge.materialize(graph)
        right_edge.materialize(graph)

    left_commits: Optional[List[str]] = compute_unmerged_commits(
        remote=remote, target_branch=config.target,
        upstream_branch=config.upstream)
    right_commits: Optional[List[str]] = compute_unmerged_commits(
        remote=remote, target_branch=config.target,
        upstream_branch=config.secondary_upstream)
    if not left_commits and not right_commits:
        print(f'- 0 unmerged commits. {config.target} is up to date.')
        return

    def printUnmergedCommits(commits: Optional[List[str]], branch: str):
        num = len(commits) if commits else 0
        print(f'- {num} unmerged commits from {branch}.')
    printUnmergedCommits(commits=left_commits, branch=config.upstream)
    printUnmergedCommits(commits=right_commits,
                         branch=config.secondary_upstream)
    if merges:
        print('- The automerger has found a common merge-base.')
        return
    print('- The automerger is waiting for unmerged commits to share')
    print(f'  a merge-base from {config.common_ancestor}')
    print('  before merging (i.e., one of the upstreams is behind).')


def print_status(remotes: List[str] = ['origin'],
                 target_branch: Optional[str] = None,
                 list_commits: bool = False,
                 query_ci_status: bool = False,
                 graph_format: Optional[str] = None):
    graph = None
    if graph_format:
        # Try to create a new graph. This will throw an exception if the
        # Graphviz package is missing.
        try:
            graph = create_graph(graph_format)
        except Exception as e:
            print(e)
            return
        # Collect all branches across remotes and create corresponding
        # subgraphs. This needs to happen before we add the edges.
        branches: List[str] = []
        for remote in remotes:
            for config in find_am_configs(remote):
                branches.append(config.upstream)
                branches.append(config.target)
                if config.secondary_upstream:
                    branches.append(config.secondary_upstream)
        add_branches(graph, branches)

    for remote in remotes:
        configs: List[AMTargetBranchConfig] = find_am_configs(remote)
        if target_branch:
            configs = list(
                filter(lambda config: config.target == target_branch, configs))
        if len(configs) == 0:
            msg = ''
            if target_branch:
                msg = f'for branch "{target_branch}" from '
            print(f'No automerger configured for {msg}remote "{remote}"')
            return

        ms = find_inflight_merges(remote)
        printed = False
        for config in configs:
            if printed:
                print('')
            if config.secondary_upstream:
                print_zippered_edge_status(config, remote, graph)
                printed = True
                continue
            print_edge_status(config.upstream, config.target,
                              ms, list_commits, remote, graph,
                              query_ci_status=query_ci_status)
            printed = True

    if graph:
        graph.render('automergers', view=True)
