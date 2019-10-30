"""
  Module for computing the automerger graph.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from git_apple_llvm.am.core import CommitStates, is_secondary_edge_commit_blocked_by_primary_edge, find_inflight_merges
from git_apple_llvm.am.oracle import get_ci_status
from git_apple_llvm.git_tools import git_output
from typing import Dict, Set, List, Optional
import sys
try:
    from graphviz import Digraph
except ImportError:
    pass


class EdgeStates:
    clear = 'clear'
    working = 'working'
    blocked = 'blocked'

    @staticmethod
    def get_color(state: str):
        colors: Dict = {
            EdgeStates.clear: 'green3',
            EdgeStates.working: 'gold3',
            EdgeStates.blocked: 'red3',
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
              common_ancestor: Optional[str] = None,
              remote: str = 'origin',
              query_ci_status: bool = False):
    commit_log_output = git_output(
        'log',
        '--first-parent',
        '--pretty=format:%H', '--no-patch',
        f'{remote}/{target_branch}..{remote}/{upstream_branch}',
    )

    if not commit_log_output:
        return EdgeStates.clear

    commits_inflight: Set[str] = set(
        inflight_merges[target_branch] if target_branch in inflight_merges else [])

    def is_blocked(commit: str):
        return common_ancestor and is_secondary_edge_commit_blocked_by_primary_edge(commit,
                                                                                    f'{remote}/{common_ancestor}',
                                                                                    f'{remote}/{target_branch}')

    def get_commit_state(commit: str):
        if query_ci_status:
            ci_state: Optional[str] = get_ci_status(commit, target_branch)
            if ci_state:
                return EdgeStates.get_state(ci_state)
        if commit in commits_inflight:
            return EdgeStates.working
        if is_blocked(commit):
            return EdgeStates.blocked
        return None

    working: bool = False
    for commit in commit_log_output.split('\n'):
        commit_state = get_commit_state(commit)
        if commit_state is EdgeStates.blocked:
            return EdgeStates.blocked
        if commit_state is EdgeStates.working:
            working = True
    if working:
        return EdgeStates.working
    return EdgeStates.clear


def print_graph(remote: str = 'origin',
                query_ci_status: bool = False,
                fmt: str = 'pdf'):
    configs: List[AMTargetBranchConfig] = find_am_configs(remote)
    if len(configs) == 0:
        print(f'No automerger configured for remote "{remote}"')
        return
    if 'graphviz' not in sys.modules:
        print(f'Generating the automerger graph requires the "graphviz" Python package.')
        return
    try:
        graph = Digraph(comment='Automergers',
                        format=fmt,
                        node_attr={'shape': 'record',
                                   'style': 'filled',
                                   'color': 'lightgray',
                                   'fixedsize': 'true',
                                   'width': '3',
                                   'height': '0.8',
                                   })
    except ValueError as e:
        print(e)
        return
    graph.attr(rankdir='LR', nodesep='1', ranksep='1')
    merges = find_inflight_merges(remote)
    for config in configs:
        edge_state = get_state(config.upstream,
                               config.target,
                               merges,
                               config.common_ancestor,
                               remote,
                               query_ci_status)
        graph.edge(config.upstream, config.target,
                   color=EdgeStates.get_color(edge_state))
        if config.secondary_upstream:
            edge_state = get_state(config.secondary_upstream,
                                   config.target,
                                   merges,
                                   config.common_ancestor,
                                   remote,
                                   query_ci_status)
            graph.edge(config.secondary_upstream, config.target,
                       color=EdgeStates.get_color(edge_state))
    graph.render('automergers', view=True)
