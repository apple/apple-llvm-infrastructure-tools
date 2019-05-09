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

For test suites that use `lit`, place a `.test` file in the subdirectory.
For test suites that use `pytest`, add the name of the subdirectory to the
`pytest_dirs` in the test makefile.

### Testing tools

- `not`: invert the exit status of a `RUN:` command.
- `check-empty`: check for empty output.
- `check-diff`: check for matching output.
- `mkrepo.sh [--bare] <repo>`: create a git repository.
- `mkblob.sh <repo> <blob>`: create and commit a blob with the given name.
- `mkrange.sh <repo> <first> <last>`: run `mkblob.sh` on `{<first>..<last>}` in
  sequence.
- `mksvn.sh <co>`: make an SVN repo at `<co>.repo` and check it out to `<co>`

## Documentation

The documentation for apple-llvm-infrastucture-tools is located in the `docs/`
subdirectory. The documentation is built using `sphinx`. Both restructured text
and the Markdown formats are supported.

To build the documentation, use the makefile located in the `docs/` subdirectory:

```
cd docs
make html
```
