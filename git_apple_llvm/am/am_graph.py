"""
    Module for computing the automerger graph.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import CommitStates, find_inflight_merges, compute_unmerged_commits, has_merge_conflict
from git_apple_llvm.am.oracle import get_ci_status
from typing import Dict, Set, List, Optional
import sys
import logging
try:
    from graphviz import Digraph
except ImportError:
    pass


log = logging.getLogger(__name__)

# Graphviz node, edge and graph attributes.
# https://www.graphviz.org/doc/info/attrs.html
NODESEP = '1'
NODE_ATTR = {'shape': 'record',
             'style': 'filled',
             'color': 'lightgray',
             'fontname': 'helvetica',
             'fixedsize': 'true',
             'width': '4',
             'height': '0.8',
             }
PENWIDTH = '2'
RANKDIR = 'LR'
RANKSEP = '1'
SPLINES = 'ortho'

# Graphviz colors.
# https://www.graphviz.org/doc/info/colors.html
GREEN = 'green3'
YELLOW = 'gold3'
RED = 'red3'


class EdgeStates:
    clear = 'clear'
    working = 'working'
    blocked = 'blocked'

    @staticmethod
    def get_color(state: str):
        colors: Dict = {
            EdgeStates.clear: GREEN,
            EdgeStates.working: YELLOW,
            EdgeStates.blocked: RED,
        }
        return colors[state]

    @staticmethod
    def get_state(commit_state: str):
        if commit_state in [CommitStates.passed]:
            return EdgeStates.clear
        if commit_state in [CommitStates.pending, CommitStates.started]:
            return EdgeStates.working
        if commit_state in [CommitStates.conflict, CommitStates.failed, CommitStates.known_failed]:
            return EdgeStates.blocked


def get_state(upstream_branch: str,
              target_branch: str,
              inflight_merges: Dict[str, List[str]],
              remote: str = 'origin',
              query_ci_status: bool = False):
    log.info(f'Computing status for [{upstream_branch} -> {target_branch}]')
    commits: Optional[List[str]] = compute_unmerged_commits(remote=remote,
                                                            target_branch=target_branch,
                                                            upstream_branch=upstream_branch,
                                                            format='%H')
    if not commits:
        return EdgeStates.clear

    commits_inflight: Set[str] = set(
        inflight_merges[target_branch] if target_branch in inflight_merges else [])

    def get_commit_state(commit: str, check_for_merge_conflict: bool):
        if query_ci_status:
            ci_state: Optional[str] = get_ci_status(commit, target_branch)
            if ci_state:
                return EdgeStates.get_state(ci_state)
        if check_for_merge_conflict and has_merge_conflict(commit, target_branch, remote):
            return EdgeStates.blocked
        if commit in commits_inflight:
            return EdgeStates.working
        return EdgeStates.clear

    # Determine the status of this edge.
    # The edge is blocked if there is a least one blocked commit. If there are
    # no blocked commits, the edge is working if there's at least one working
    # commit. Otherwise the edge is clear.
    working: bool = False
    check_for_merge_conflict: bool = True
    for commit in commits:
        commit_state = get_commit_state(commit, check_for_merge_conflict)
        if commit_state is EdgeStates.blocked:
            return EdgeStates.blocked
        if commit_state is EdgeStates.working:
            working = True
        # Only check for a merge conflict on the first commit.
        check_for_merge_conflict = False
    if working:
        return EdgeStates.working
    return EdgeStates.clear


def create_subgraph(graph, name: str, nodes: List[str]):
    log.info(f'Creating {name} subgraph with {len(nodes)} node(s)')
    with graph.subgraph(name=f'cluster_{name}') as subgraph:
        subgraph.attr(label=name)
        for node in nodes:
            subgraph.node(node)


def add_branches(graph, branches: List[str]):
    llvm: List[str] = []
    github: List[str] = []
    internal: List[str] = []

    branches = sorted(set(branches))
    for branch in branches:
        if branch.startswith('llvm'):
            llvm.append(branch)
            continue
        if branch.startswith('swift'):
            github.append(branch)
            continue
        if branch.startswith('apple'):
            github.append(branch)
            continue
        internal.append(branch)

    create_subgraph(graph, 'LLVM', llvm)
    create_subgraph(graph, 'Github', github)
    create_subgraph(graph, 'Internal', internal)


def print_graph(remotes: List = ['origin'],
                query_ci_status: bool = False,
                fmt: str = 'pdf'):
    if 'graphviz' not in sys.modules:
        print(f'Generating the automerger graph requires the "graphviz" Python package.')
        return
    try:
        graph = Digraph(comment='Automergers',
                        format=fmt,
                        node_attr=NODE_ATTR)
        graph.attr(rankdir=RANKDIR,
                   nodesep=NODESEP,
                   ranksep=RANKSEP,
                   splines=SPLINES)
    except ValueError as e:
        print(e)
        return

    # Collect all branches and create corresponding subgraphs.
    branches: List[str] = []
    for remote in remotes:
        for config in find_am_configs(remote):
            branches.append(config.upstream)
            branches.append(config.target)
            if config.secondary_upstream:
                branches.append(config.secondary_upstream)
    add_branches(graph, branches)

    # Create the edges.
    for remote in remotes:
        configs: List[AMTargetBranchConfig] = find_am_configs(remote)
        if len(configs) == 0:
            print(f'No automerger configured for remote "{remote}"')
            continue
        merges = find_inflight_merges(remote)
        for config in configs:
            edge_state = get_state(config.upstream,
                                   config.target,
                                   merges,
                                   remote,
                                   query_ci_status)
            graph.edge(config.upstream, config.target,
                       color=EdgeStates.get_color(edge_state),
                       penwidth=PENWIDTH)
            if config.secondary_upstream:
                edge_state = get_state(config.secondary_upstream,
                                       config.target,
                                       merges,
                                       remote,
                                       query_ci_status)
                graph.edge(config.secondary_upstream, config.target,
                           color=EdgeStates.get_color(edge_state),
                           penwidth=PENWIDTH, constraint='false')
    graph.render('automergers', view=True)
