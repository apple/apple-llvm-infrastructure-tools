import os, sys
import lit.formats

config.name = 'lit-example'
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = ['.test']

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(os.path.dirname(__file__), '.lit_test_output')
