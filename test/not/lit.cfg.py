import os, sys
import lit.formats

config.name = 'not'
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = ['.test']

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(os.path.dirname(__file__), '.lit_test_output')

not_test_source_root = os.path.dirname(__file__)
test_source_root = os.path.dirname(not_test_source_root)
src_root = os.path.dirname(test_source_root)
bindir = os.path.join(src_root, 'bin')
testbindir = os.path.join(test_source_root, 'bin')

# Update the path.
path = config.environment['PATH']
config.environment['PATH'] = os.path.pathsep.join((bindir, testbindir, path))
