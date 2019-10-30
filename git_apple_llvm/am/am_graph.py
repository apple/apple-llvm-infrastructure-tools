"""
  Module for computing the automerger graph.
"""

from git_apple_llvm.am.am_config import find_am_configs, AMTargetBranchConfig
from typing import List
import sys
try:
    from graphviz import Digraph
except ImportError:
    pass


def print_graph(remote: str = 'origin', fmt: str = 'pdf'):
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
    for config in configs:
        graph.edge(config.upstream, config.target)
    graph.render('automergers', view=True)
