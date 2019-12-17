"""
    Module for computing the automerger graph.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import CommitStates, find_inflight_merges, compute_unmerged_commits, has_merge_conflict
from git_apple_llvm.am.oracle import get_ci_status
from git_apple_llvm.am.zippered_merge import compute_zippered_merges
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
BLUE = 'blue3'
YELLOW = 'gold3'
RED = 'red3'


class Subgraph:

    def __init__(self, name: str):
        self.name: str = name
        self.nodes: List[str] = []
        self.subgraphs: Dict[str, Subgraph] = dict()

    def add_node(self, node: str):
        self.nodes.append(node)

    def __getitem__(self, name):
        if name not in self.subgraphs:
            self.subgraphs[name] = Subgraph(name)
        return self.subgraphs[name]

    def materialize(self, graph):
        log.info(f'Creating {self.name} subgraph with {len(self.nodes)} nodes '
                 f'and {len(self.subgraphs)} nested subgraphs.')
        with graph.subgraph(name=f'cluster_{self.name}') as subgraph:
            subgraph.attr(label=self.name)
            for node in self.nodes:
                subgraph.node(node)
            for _, subsubgraph in self.subgraphs.items():
                subsubgraph.materialize(subgraph)


class EdgeStates:
    clear = 'clear'
    waiting = 'waiting'
    working = 'working'
    blocked = 'blocked'

    @staticmethod
    def get_color(state: str):
        colors: Dict = {
            EdgeStates.clear: GREEN,
            EdgeStates.waiting: BLUE,
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


def get_zippered_state(config: AMTargetBranchConfig, remote: str):
    log.info(f'Computing status for [{config.upstream} -> {config.target} <- {config.secondary_upstream}]')
    merges: Optional[List[List[str]]] = []
    merges = compute_zippered_merges(remote=remote, target=config.target,
                                     left_upstream=config.upstream,
                                     right_upstream=config.secondary_upstream,
                                     common_ancestor=config.common_ancestor,
                                     stop_on_first=True)
    if merges:
        return (EdgeStates.working, EdgeStates.working)

    left_commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=config.target,
                                                                 upstream_branch=config.upstream)
    right_commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=config.target,
                                                                  upstream_branch=config.secondary_upstream)

    left_state = EdgeStates.waiting if left_commits else EdgeStates.clear
    right_state = EdgeStates.waiting if right_commits else EdgeStates.clear
    return (left_state, right_state)


def create_subgraph(graph, name: str, nodes: List[str]):
    log.info(f'Creating {name} subgraph with {len(nodes)} node(s)')
    with graph.subgraph(name=f'cluster_{name}') as subgraph:
        subgraph.attr(label=name)
        for node in nodes:
            subgraph.node(node)


def add_branches(graph, branches: List[str]):
    llvm = Subgraph('LLVM')
    swift = Subgraph('Swift')
    internal = Subgraph('Internal')

    branches = sorted(set(branches))
    for branch in branches:
        if branch.startswith('llvm'):
            llvm.add_node(branch)
            continue
        if branch.startswith('swift'):
            swift.add_node(branch)
            continue
        if branch.startswith('apple'):
            swift.add_node(branch)
            continue
        internal.add_node(branch)

    llvm.materialize(graph)
    swift.materialize(graph)
    internal.materialize(graph)


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
            if config.secondary_upstream:
                left_state, right_state = get_zippered_state(config, remote)
                graph.edge(config.upstream, config.target,
                           color=EdgeStates.get_color(left_state),
                           penwidth=PENWIDTH)
                graph.edge(config.secondary_upstream, config.target,
                           color=EdgeStates.get_color(right_state),
                           penwidth=PENWIDTH, constraint='false')
            else:
                edge_state = get_state(config.upstream,
                                       config.target,
                                       merges,
                                       remote,
                                       query_ci_status)
                graph.edge(config.upstream, config.target,
                           color=EdgeStates.get_color(edge_state),
                           penwidth=PENWIDTH)
    graph.render('automergers', view=True)
