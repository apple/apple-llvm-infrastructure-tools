"""
  Module for working with AM configuration.
"""

from git_apple_llvm.git_tools import read_file_or_none
from typing import Optional, Dict, List
import json


class AMTargetBranchConfig:
    """
    Represents a configuration for an automerger target branch.

    e.g.
    {
        "target": "master-next"
        "upstream": "master"
    }

    Attributes
    ----------
    target : str
    The name of the branch that is the automerger target.

    upstream : str
    The name of the upstream branch that merges into this branch.

    secondary_upstream : Optional[str]
    The name of the optional secondary upstream that merges into this branch once a common ancestor point is
    merged through upstream.

    test_command : str
    The name of the command to run for testing the merge.
    """

    def __init__(self, json: Dict):
        if 'upstream' not in json or 'target' not in json:
            raise RuntimeError('invalid AM config')
        self.target: str = json['target']
        self.upstream: str = json['upstream']
        self.secondary_upstream: Optional[str] = json['secondary-upstream'] if 'secondary-upstream' in json else None
        self.common_ancestor: Optional[str] = json['common-ancestor'] if 'common-ancestor' in json else None
        if self.secondary_upstream and not self.common_ancestor:
            raise RuntimeError(
                'invalid AM config: missing common ancestor for a secondary upstream')
        self.test_command: Optional[str] = json['test-command'] if 'test-command' in json else None

    def __repr__(self) -> str:
        return f'[AM Target: {self.upstream} -> {self.target}]'


def find_am_configs(remote: str = 'origin') -> List[AMTargetBranchConfig]:
    contents: Optional[str] = read_file_or_none(
        f'{remote}/repo/apple-llvm-config/am', f'apple-llvm-config/am/am-config.json')
    if not contents:
        return []
    configs = json.loads(contents)
    if not configs:
        return []
    return [AMTargetBranchConfig(json_dict) for json_dict in configs]


def find_am_config_dict(remote: str = 'origin') -> Dict[str, AMTargetBranchConfig]:
    configs: Dict[str, AMTargetBranchConfig] = {}
    for config in find_am_configs(remote):
        if config.target in configs:
            raise RuntimeError(f'invalid AM config, multiple {config.target} branches')
        # FIXME: Verify that the remote has the branch.
        configs[config.target] = config
    return configs


def read_config_for_branch(branch: str, remote: str = 'origin') -> Optional[AMTargetBranchConfig]:
    configs: Dict[str, AMTargetBranchConfig] = find_am_config_dict(remote)
    if branch in configs:
        return configs[branch]
    return None
