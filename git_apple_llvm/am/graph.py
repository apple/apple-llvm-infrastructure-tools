"""
    Module for computing the automerger graph.
"""

from git_apple_llvm.am.am_config import AMTargetBranchConfig
from git_apple_llvm.am.core import CommitStates, compute_unmerged_commits, has_merge_conflict
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


class Edge:

    def __init__(self, upstream: str, target: str):
        self.upstream: str = upstream
        self.target: str = target
        self.state: str = EdgeStates.clear
        self.url: Optional[str] = None
        self.constraint: bool = True

    def set_state(self, state: str):
        self.state = state

    def set_constraint(self, constraint: bool):
        self.constraint = constraint

    def materialize(self, graph):
        contraint = 'true' if self.constraint else 'false'
        graph.edge(self.upstream, self.target,
                   color=EdgeStates.get_color(self.state),
                   URL=self.url,
                   penwidth=PENWIDTH,
                   constraint=contraint)


def compute_edge(upstream_branch: str,
                 target_branch: str,
                 commits_inflight: Set[str],
                 commits_unmerged: Optional[List[str]],
                 remote: str = 'origin',
                 query_ci_status: bool = False):
    log.info(f'Computing edge for [{upstream_branch} -> {target_branch}]')
    edge: Edge = Edge(upstream_branch, target_branch)
    if not commits_unmerged:
        return edge

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
    for commit in commits_unmerged:
        commit_state = get_commit_state(commit, check_for_merge_conflict)
        if commit_state is EdgeStates.blocked:
            edge.set_state(EdgeStates.blocked)
            return edge
        if commit_state is EdgeStates.working:
            working = True
        # Only check for a merge conflict on the first commit.
        check_for_merge_conflict = False
    if working:
        edge.set_state(EdgeStates.working)
    return edge


def compute_zippered_edges(config: AMTargetBranchConfig, remote: str, merges: Optional[List[List[str]]]):
    log.info(f'Computing edges for [{config.upstream} -> {config.target} <- {config.secondary_upstream}]')

    left_edge: Edge = Edge(config.upstream, config.target)
    right_edge: Edge = Edge(config.secondary_upstream, config.target)
    right_edge.set_constraint(False)
    if merges:
        left_edge.set_state(EdgeStates.working)
        right_edge.set_state(EdgeStates.working)
        return (left_edge, right_edge)

    left_commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=config.target,
                                                                 upstream_branch=config.upstream)
    left_edge.set_state(
        EdgeStates.waiting if left_commits else EdgeStates.clear)
    right_commits: Optional[List[str]] = compute_unmerged_commits(remote=remote, target_branch=config.target,
                                                                  upstream_branch=config.secondary_upstream)
    right_edge.set_state(
        EdgeStates.waiting if right_commits else EdgeStates.clear)
    return (left_edge, right_edge)


def create_subgraph(graph, name: str, nodes: List[str]):
    log.info(f'Creating {name} subgraph with {len(nodes)} node(s)')
    with graph.subgraph(name=f'cluster_{name}') as subgraph:
        subgraph.attr(label=name)
        for node in nodes:
            subgraph.node(node)


def add_branches(graph, branches: List[str]):
    llvm = Subgraph('github.com/llvm')
    apple = Subgraph('github.com/apple')
    internal = Subgraph('Internal')

    branches = sorted(set(branches))
    for branch in branches:
        if branch.startswith('llvm'):
            llvm.add_node(branch)
            continue
        # Check `internal` before `apple`.
        elif (branch.startswith('internal')
                or branch.startswith('swift/internal')):
            internal.add_node(branch)
            continue
        elif (branch == 'next'
                or branch.startswith('swift')
                or branch.startswith('apple')
                or branch.startswith('stable')):
            apple.add_node(branch)
            continue
        raise ValueError('Unknown branch "{}"'.format(branch))

    llvm.materialize(graph)
    apple.materialize(graph)
    internal.materialize(graph)


def create_graph(fmt: str = 'pdf'):
    if 'graphviz' not in sys.modules:
        raise RuntimeError('Generating the automerger graph requires the "graphviz" Python package.')
    graph = Digraph(comment='Automergers',
                    format=fmt,
                    node_attr=NODE_ATTR)
    graph.attr(rankdir=RANKDIR,
               nodesep=NODESEP,
               ranksep=RANKSEP,
               splines=SPLINES)
    return graph
