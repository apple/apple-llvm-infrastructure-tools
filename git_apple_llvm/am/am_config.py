"""
  Module for working with AM configuration.
"""

from git_apple_llvm.git_tools import git_output, read_file_or_none
from typing import Optional, Dict, List
import json


class AMTargetBranchConfig:
    """
    Represents a configuration for an automerger target branch.

    e.g.
    {
        "upstream": "master"
    }

    Attributes
    ----------
    upstream : str
    The name of the upstream branch that merges into this branch.

    secondary_upstream : Optional[str]
    The name of the optional secondary upstream that merges into this branch once a common ancestor point is
    merged through upstream.

    test_command : str
    The name of the command to run for testing the merge.
    """

    def __init__(self, branch: str, json: Dict):
        if 'upstream' not in json:
            raise RuntimeError('invalid AM config')
        self.target: str = branch
        self.upstream: str = json['upstream']
        self.secondary_upstream: Optional[str] = json['secondary-upstream'] if 'secondary-upstream' in json else None
        self.common_ancestor: Optional[str] = json['common-ancestor'] if 'common-ancestor' in json else None
        if self.secondary_upstream and not self.common_ancestor:
            raise RuntimeError(
                'invalid AM config: missing common ancestor for a secondary upstream')
        self.test_command: Optional[str] = json['test-command'] if 'test-command' in json else None

    def __repr__(self) -> str:
        return f'[AM Target: {self.upstream} -> {self.target}]'


def read_config_for_branch(branch: str, remote: str = 'origin') -> Optional[AMTargetBranchConfig]:
    config_name: str = branch.replace('/', '-')
    contents: Optional[str] = read_file_or_none(
        f'{remote}/{branch}', f'apple-llvm-config/am/{config_name}.json')
    if not contents:
        return None
    return AMTargetBranchConfig(branch, json.loads(contents))


def find_am_configs() -> List[AMTargetBranchConfig]:
    remote = 'origin'
    ref_prefix = f'refs/remotes/{remote}/'
    refs = git_output('branch', '--list',
                      '--format=%(refname)', '-r').split('\n')
    configs: List[AMTargetBranchConfig] = []
    for ref in refs:
        if not ref.startswith(ref_prefix):
            continue
        branch = ref[len(ref_prefix):]
        config: Optional[AMTargetBranchConfig] = read_config_for_branch(
            branch, remote)
        if not config:
            continue
        configs.append(config)
    return configs
