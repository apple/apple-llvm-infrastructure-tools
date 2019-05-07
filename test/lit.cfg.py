import os, sys
import lit.formats

config.name = 'apple-llvm'
config.test_format = lit.formats.ShTest(execute_external=False)

# suffixes: A list of file extensions to treat as test files.
config.suffixes = ['.test']

# test_source_root: where to run the tests from
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.test_source_root, 'Run.lit')

src_root = os.path.dirname(config.test_source_root)
bindir = os.path.join(src_root, 'bin')
testbindir = os.path.join(config.test_source_root, 'bin')

# Update the path.
path = config.environment['PATH']
config.environment['PATH'] = os.path.pathsep.join((bindir, testbindir, path))
