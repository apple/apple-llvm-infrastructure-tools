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
        dot = Digraph(comment='Automergers',
                      format=fmt,
                      node_attr={'shape': 'record'})
    except ValueError as e:
        print(e)
        return
    dot.node_attr.update(style='filled', color='lightblue')
    for config in configs:
        dot.edge(config.upstream, config.target)
    dot.render('automergers', view=True)
