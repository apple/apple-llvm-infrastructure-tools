# Rebasing a branch based on split history onto the monorepo

If you have a branch based on a split history at
[github.com/apple/swift-clang/](https://github.com/apple/swift-clang/),
[github.com/apple/swift-llvm/](https://github.com/apple/swift-llvm/),
[github.com/apple/swift-compiler-rt/](https://github.com/apple/swift-compiler-rt/),
[github.com/apple/swift-clang-tools-extra/](https://github.com/apple/swift-clang-tools-extra/),
or [github.com/apple/swift-libcxx/](https://github.com/apple/swift-libcxx/),
which were downstreams of the LLVM project's git-svn mirrors, and want to
rebase your branch onto
[github.com/apple/llvm-project](https://github.com/apple/llvm-project), which
is downstream of the LLVM project's monorepo at
[github.com/llvm/llvm-project](https://github.com/llvm/llvm-project), then this
guide is for you.

### TL;DR

```sh
$ git rebase -Xsubtree=llvm                            \
    --onto github/apple/llvm-project/apple/master      \
    github/apple/swift-llvm/upstream-with-swift
$ git rebase -Xsubtree=clang                           \
    --onto github/apple/llvm-project/apple/master      \
    github/apple/swift-clang/upstream-with-swift
$ git rebase -Xsubtree=lldb                            \
    --onto github/apple/llvm-project/swift/master-next \
    github/apple/swift-lldb/upstream-with-swift
```

## Steps to rebase a split branch onto the monorepo

1. Add your fork of [llvm-project](https://github.com/apple/llvm-project) as an
   extra remote to your existing checkout.
2. Checkout the split branch you want to convert.
3. Rebase with `--onto <monobase>` and `-Xsubtree=<dir>` (the interesting
   part).
4. Push to your fork.
5. Continue work from a fresh clone of your llvm-project fork.

## An example of rebasing a split branch onto the monorepo

Here's the scenario:

- I have a branch called `bugfix` that's based on the `upstream-with-swift`
  branch at [swift-llvm](https://github.com/apple/swift-llvm/).
- I want to convert over to
  [llvm-project](https://github.com/apple/llvm-project), with
  a final branch based on `apple/master`.
- I'm using GitHub and I already have forks of both repositories.
- My GitHub username is in the environment variable `USERNAME`.

### Adding an extra remote to the checkout

Clone swift-llvm if you haven't already:

```sh
$ git clone git@github.com:$USERNAME/swift-llvm.git
$ cd swift-llvm
```

Add a remote for the official `swift-llvm` history from Apple:
```sh
$ git remote add upstream/split               \
    git@github.com:apple/swift-llvm.git
```

Now add remotes for llvm-project.

```sh
$ git remote add monorepo                     \
    git@github.com:$USERNAME/llvm-project.git
$ git remote add upstream/monorepo            \
    git@github.com:apple/llvm-project.git
```

Fetching the contents will take some time:

```sh
$ git remote update
```

### Checkout the branch you want to convert

The branch I want to convert is called "bugfix".  There are lots of ways to
check it out, but here's one:

```sh
$ git checkout --detach origin/bugfix
```

### Rebase (the interesting part)

The key command is `git-rebase` (see `man git-rebase` for details).  The form
we want to use is:

```sh
$ git rebase -Xsubtree=<dir> --onto <newbase> <upstream> [<branch>]
```

Here's what the arguments mean:

- `<dir>` is the name of the LLVM sub-project.  `-Xsubtree=<dir>` will put all
  the rebased changes into `<dir>/`.
- `<newbase>` is where to build the new history.  Usually you don't need this,
  but in this case we need it to build on top of the monorepo.
- `<upstream>` is what the current history is built on top of, which should be
  a split repo commit that already has an equivalent monorepo commit.
- `<branch>`, if specified, is which branch to checkout and rebase.  If not
  specified, `git-rebase` operates on the currently-checked-out branch
  (`HEAD`).

Since we already have `bugfix` checked out, we just need:

```sh
$ git rebase -Xsubtree=llvm               \
    --onto upstream/monorepo/apple/master \
    upstream/split/upstream-with-swift
```

Alternatively we could have skipped the previous step with:

```sh
$ git rebase -Xsubtree=llvm               \
    --onto upstream/monorepo/apple/master \
    upstream/split/upstream-with-swift    \
    origin/bugfix
```

### Push to your fork

Now you just need to push `HEAD` to the monorepo fork.  For example, to use the
same branch name, `bugfix`:

```sh
$ git push monorepo HEAD:bugfix
```

### Continuing working from a fresh clone of your llvm-project fork

Continue working on your newly-converted monorepo branch in a fresh clone.

```sh
$ cd ..
$ git clone git@github.com:$USERNAME/llvm-project.git
$ git checkout -b bugfix origin/bugfix
```

## Advanced: Rebasing onto *the same* commit in the monorepo

The above instructions implicitly rebase your history on top of the newest
`apple/master` commit.  If you want to translate your branch to the monorepo
and have it built on the monorepo version of *the same* commit it's based on
now, you need to be more precise.

### Find your split merge base

First, you need to find your current merge base with the split upstream:

```sh
$ BASE=$(git merge-base HEAD upstream/split/upstream-with-swift)
```

### Find equivalent monorepo base

Then you need to find the equivalent monorepo commit.

#### llvm.org merge bases

If this is an llvm.org upstream commit with a `git-svn-id:` trailer, you can
grab the SVN revision number:

```sh
$ REV=$(git log --no-walk --format=%B $BASE |
        grep git-svn-id:)
$ REV=${REV#*@}
$ REV=${REV%%#}
```

Then find the monorepo commit by looking for an equivalent `llvm-svn:` trailer:

```sh
# Look at the log.  There should just be one.
$ git log upstream/monorepo/apple/master \
    --grep "^llvm-svn: $REV\$"

# Extract the commit hash.
$ MONOBASE=$(git log --format=%H --no-walk    \
               upstream/monorepo/apple/master \
               --grep "^llvm-svn: $REV\$"
```

#### github.com/apple merge bases

If this is a downstream commit from Apple's split repos (like swift-llvm), it
should have been converted as part of the monorepo transition.  You can find it
directly by looking for the `apple-llvm-split-commit:` trailer.

```sh
# Look at the log.  There should just be one.
$ git log upstream/monorepo/apple/master \
    --grep "^apple-llvm-split-commit: $BASE\$"

# Extract the commit hash.
$ MONOBASE=$(git log --format=%H --no-walk    \
               upstream/monorepo/apple/master \
               --grep "^apple-llvm-split-commit: $BASE\$"
```

### Rebase onto the monorepo base

We find the split merge base and its equivalent monorepo base.  Now we're ready
to run the command as before:

```sh
$ git rebase -Xsubtree=llvm --onto $MONOBASE $BASE
```
