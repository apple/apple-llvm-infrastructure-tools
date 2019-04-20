# Tools for working downstream with the LLVM project

## Deploying `git-apple-llvm`

Add the embedded `bin` to your `PATH`.  That's it for now; the C++ code gets
compiled on-demand.

## mt: Tools for the monorepo transition

### Maps maintained in refs

A collection of tools for working downstream with the LLVM project.

#### Revision to monorepo commit

```
refs/mt/svn2git -> <blob>
```

The `<blob>` is a binary file that maps from an SVN revision number on
[llvm.org](http://llvm.org/) to the monorepo commit on
github/llvm/llvm-project.  The SHA1s for the commits are each packed into 20
bytes and stored contiguously by revision, leaving 0s for revisions that don't
have commits mapped (yet).

Looking up a commit is easy using `xxd`.  For example, to look up r20343, run:

```
FILE=$(git unpack-file refs/mt/svn2git)
xxd -s $(( 20 * 20343 )) -g 0 -c 20 -l 20 -p $FILE
```

#### Split repository commit to monorepo commit

```
refs/mt/remotes/<remote>/split2mono -> <blob>
```

This `<blob>` maps from a commit in a split Git repo to its translated
monorepo commit.  We probably want want to use sqlite3 for this.

This database must be saved separately for each remote.

- We want a global view of commits.
    - The split repos have complex commit graphs.  It's vital that each
      split commit is only translated once to a monorepo commit.
- Downstream remotes have more commits than upstream.
    - There could be secrets.  It's important to make it difficult to
      accidentally push a downstream database to an upstream remote.
- We want access to be fast.

As a result, we need a way of merging from an upstream db into a
downstream.

### Tools for maintaining monorepo transition branches

#### Dealing with SVN revisions from [llvm.org](http://llvm.org/)

`git-mt-llvm-svn2git` converts an SVN revision number to a monorepo commit.

`git-mt-llvm-svn` extracts an SVN revision number from a commit
message.  We need to support a few different formats:

- `llvm-svn: <rev>`, which is used by github/llvm/llvm-project
- `git-svn-id: https://llvm.org/svn/llvm-project/...@<rev> ...`,
  which is what's in our Git history
- Note: in the past, `git-interpret-trailers` has choked on some of
  these; e.g., from LLVM:
    - r12026: ce6096f49bd5741a37116e1bf9df501600a631d3
    - r12457: 24ad00db5a7f86b1ce725d831058347d62edfe8b
    - r28137: b72773bb88859633c62bc4938d05aafedb1442f1
    - r47672: 077f9b20d0e8659d00a09046a63e28edf0665ffe
    - ...

`git-mt-llvm-svn2git-map` updates the map from SVN revision numbers
from [llvm.org](http://llvm.org/) to monorepo commits on
github/llvm/llvm-project, filling up the svn2git map.

- pass in a commit from the canonical monorepo, and it will map all
  the commits in its history
- subsequent calls to `git-mt-llvm-svn2git` will be able to answer queries

#### Mapping split repo versions of llvm.org

`git-mt-split2mono-map-llvm` maps split repo open source LLVM commits to their
canonical monorepo commits, filling up the split2mono map.

Note: it's not clear if we need to use this.

- It *looks* like we can detect cherry-picks in `git-mt-llvm-svn` by comparing
  the committer and author, which means it's safe for `git-mt-split2mono` to
  defer to `git-mt-llvm-svn` and `git-mt-llvm-svn2git` (leveraging
  `git-svn-id:`).  But maybe we'll find a counterexample.
- It *seems* like it's not valuable to explicitly map split LLVM commit
  histories, which would bloat the maps unnecessarily.  But perhaps
  `git-mt-split2mono` is too slow without this.

#### Dealing with downstream, split-repo branches

There are a few tools sketched out so far:

`git-mt-split2mono`, to look up an existing mapping

- Expects split commits from upstream to have been mapped using
  `git-mt-split2mono-map-llvm`.

`git-mt-split2mono-translate-commit`, to create monorepo commits out of split
repo commits

- Leave commits handled by `git-mt-llvm-svn2git` where they are?

`git-mt-split2mono-translate-branch` translates commits onto a branch.
It's given:

- `<branch>` the name of the branch to add commits to
- `<upstream>...` a list of upstream monorepo branches
- `<skip>...` a list of known-to-already-be-mapped refs to skip looking
  at in the split repos, as a performance optimization
- `<ref>:<dir>...` a list of refs from split repositories and which
  directory to move them to, where `<ref>:` on its own indicates it
  should go at root (similar to
  [llvm.org/git/monorepo-root.git](http://git.llvm.org/git/monorepo-root.git)).
