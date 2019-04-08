# Tools for working downstream with the LLVM project

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

```

`git-mt-llvm-svn2git-map` updates the map from SVN revision numbers from
llvm.org (http://llvm.org/) to monorepo commits on github/llvm/llvm-project

- pass in a commit from the canonical monorepo, and it will map all
  the commits in its history
- subsequent calls to `git-mt-llvm-svn2git` will be able to answer queries
