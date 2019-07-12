# git-apple-llvm: push

`git apple-llvm push` is a tool for pushing commits from a monorepo to the split repositories
that were used to generate that monorepo.

This tool only works with branches that are specifically configured for pushes.

## Current syntax

```
git apple-llvm push <source-ref>:<destination-ref> [--dry-run] [--verbose]
```

Where:

- `source-ref` is the Git reference to the commit you want to push. Use `HEAD` to refer to the currently checked out commit.
- `destination-ref` is the name of the monorepo branch you want to push to. If the branch cannot be pushed to, the tool will report an error.

In general, you will probably want to use a workflow like this to determine what to push:

```
git checkout <monorepo-branch>
# make changes & commit them
git apple-llvm push HEAD:<monorepo-branch>
```

The push always works with the `origin` Git remote.

## Examples

```
cd llvm-project-v1
git checkout apple/master
touch clang/new-clang-file
git commit -am "new file in clang on apple/master"
git apple-llvm push HEAD:apple/master
```

is equivalent to:

```
cd swift-clang
git checkout upstream-with-swift
touch new-clang-file
git commit -am "new file in clang on apple/master"
git push origin HEAD:upstream-with-swift
```

## Known issues

- It cannot be used with upstream llvm.org monorepo (yet).
- The tools clones the monorepo into a hidden directory that contains the split repo .Git. It needs to do it when doing a first push to a new split directory. That is quite slow (1-2 mins), but subsequent pushes to the same split repo will be faster.
- Since the monorepo needs to be regenerated, changes pushed to split repos will not show up as monorepo commits immediately. It might take up to 30 minutes for the monorepo commit to be created after a push occurred.
