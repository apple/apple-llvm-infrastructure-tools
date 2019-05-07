# Contributing to apple-llvm tools

The set of tools in the `git apple-llvm` umbrella are located in the
apple-llvm-infrastructure-tools repository.

## Testing guide

The tests for apple-llvm-infrastructure-tools are located in the `test/`
subdirectory. You can run all the tests using the makefile located in that
directory:

```
cd test
make
```

Different test suites are located in separeate subdirectories inside of `test/`.
Each test suite is invoked from the makefile, either using `pytest`, or using
`lit`.

### Adding a new test suite

A new test suite can be added by creating a subdirectory under `test/`.

## Documentation

The documentation for apple-llvm-infrastucture-tools is located in the `docs/`
subdirectory. The documentation is built using `sphinx`. Both restructured text
and the Markdown formats are supported.

To build the documentation, use the makefile located in the `docs/` subdirectory:

```
cd docs
make html
```
