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
- `check-diff <script> <check-prefix> <temp>`: check for matching output.  This
  is a lightweight tool similar to `FileCheck`, lacking regex support.
  Extracts lines containing `<check-prefix>:` from `<script>` and runs `diff`
  against stdin ignoring whitespace.  `<temp>.<check-prefix>.d` is used as a
  temp directory.
- `mkcommit <repo> ...`: wrapper around git-commit.
- `number-commits <repo> ...`: wrapper around git-rev-list that creates a map.
- `apply-commit-numbers <map>`: filter input using map from `number-commits`.
- `mkrepo [--bare] <repo>`: create a git repository.
- `mkblob <repo> <blob>`: create and commit a blob with the given name.
- `mkblob-svn [options] rev [msg...]`: create and commit blobs in a way that
  mimics SVN commits in either (or both) git-svn and llvm style.
- `mkrange <repo> <first> <last>`: run `mkblob.sh` on `{<first>..<last>}` in
  sequence.
- `mkmerge <repo> <id> <args>...`: create a merge commit from `<args>` using
  `<id>` in the subject of the commit message.  Uses `--no-ff` so there is
  always a merge commit.

## Documentation

The documentation for apple-llvm-infrastucture-tools is located in the `docs/`
subdirectory. The documentation is built using `sphinx`. Both restructured text
and the Markdown formats are supported.

To build the documentation, use the makefile located in the `docs/` subdirectory:

```
cd docs
make html
```
