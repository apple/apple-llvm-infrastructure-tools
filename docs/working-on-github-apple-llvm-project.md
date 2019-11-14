# Working with Apple's downstream llvm-project

This guide is aimed at engineers using Apple's downstream
[llvm-project](https://github.com/apple/llvm-project).

## Building Swift

You can use Swift's `update-checkout` script to get the monorepo! The following command can be used when working with Swift's `master` branch:

```
./swift/utils/update-checkout --scheme master-llvm-monorepo --clone-with-ssh --symlink-llvm-monorepo
```

Alternatively, you can use the following command when working with Swift's `master-next` branch:

```
./swift/utils/update-checkout --scheme master-next-llvm-monorepo --clone-with-ssh --symlink-llvm-monorepo
```

## Branching scheme

There are currently three namespaces for branches on
[github.com/apple/llvm-project](https://github.com/apple/llvm-project):

- `llvm.org/*`: These branches are forwarded, unchanged, from
  [github.com/llvm/llvm-project](https://github.com/llvm/llvm-project).  These
  are read-only, exact copies of the upstream LLVM project's branches.  They
  are forwarded here as a convenience for easy reference, to avoid the need for
  extra remotes.
    - [llvm.org/master](https://github.com/apple/llvm-project/tree/llvm.org/master)
      is the most important branch here, matching the LLVM project's
      [master](https://github.com/llvm/llvm-project/tree/master) branch.
- `apple/*`: These branches have downstream content, besides what is in the
  LLVM project.  This content includes some patches that have not yet been
  fully upstreamed to the LLVM project, including special support for Swift.
  Critically, however, none of these branches *depend on* the
  [github.com/apple/swift](https://github.com/apple/swift) repository.
    - [apple/master](https://github.com/apple/llvm-project/tree/apple/master)
      is directly downstream of
      [llvm.org/master](https://github.com/apple/llvm-project/tree/llvm.org/master).
      There is a gated automerger that does testing before merging in.  There
      are currently a few non-trivial differences from upstream, but the goal
      is to minimize this difference, and eventually to match LLVM project
      exactly.  Most changes to this branch should be redirected to
      <https://reviews.llvm.org/> (see also
      <http://llvm.org/docs/Contributing.html>).
    - `apple/stable/*`: These branches are periodic stabilization branches,
      where fixes are cherry-picked from LLVM.  At time of writing:
        - [apple/stable/20191106](https://github.com/apple/llvm-project/tree/apple/stable/20191106)
          is the most recent stabilization branch.
        - [apple/stable/20190619](https://github.com/apple/llvm-project/tree/apple/stable/20190619)
          is the current stabilization branch for
          [swift/master](https://github.com/apple/llvm-project/tree/swift/master)
          (see below).
- `swift/*`: These branches are downstream of `apple/*`, additionally adding
  LLDB dependencies on [Swift](https://github.com/apple/swift).  Each branch is
  automerged from a branch in the `apple/*` namespace, and they should have
  zero differences outside the `lldb/` and `apple-llvm-config/` directories.
  The naming scheme is `swift/<swift-branch>`, where `<swift-branch>` is the
  aligned Swift branch.  These are the most important branches:
    - [swift/master-next](https://github.com/apple/llvm-project/tree/swift/master-next)
      is downstream of [apple/master](https://github.com/apple/llvm-project/tree/apple/master)
      and aligned with Swift's
      [master-next](https://github.com/apple/swift/tree/master-next) branch.
    - [swift/master](https://github.com/apple/llvm-project/tree/swift/master)
      is downstream of a stabilization branch in `apple/stable/*`
      ([apple/stable/20190619](https://github.com/apple/llvm-project/tree/apple/stable/20190619),
      as of time of writing) and aligned with Swift's
      [master](https://github.com/apple/swift/tree/master) branch.

### Historical trivia: mappings to branches from before the monorepo transition

Before the monorepo transition, Apple maintained downstream forks of various
split repositories.  Here is a mapping from a few of the new branches in the
llvm-project monorepo to their original split repositories.

- [apple/master](https://github.com/apple/llvm-project/tree/apple/master) was
  generated from the `upstream-with-swift` branches in
  [swift-clang](https://github.com/apple/swift-clang/),
  [swift-llvm](https://github.com/apple/swift-llvm/),
  [swift-compiler-rt](https://github.com/apple/swift-compiler-rt/),
  [swift-clang-tools-extra](https://github.com/apple/swift-clang-tools-extra/),
  and [swift-libcxx](https://github.com/apple/swift-libcxx/), with the notable
  **exclusion** of [swift-lldb](https://github.com/apple/swift-lldb/),
- [swift/master-next](https://github.com/apple/llvm-project/tree/swift/master-next)
  was generated from the `upstream-with-swift` branch in
  [swift-lldb](https://github.com/apple/swift-lldb/), interleaved with merges
  from [apple/master](https://github.com/apple/llvm-project/tree/apple/master).
- [apple/stable/20190104](https://github.com/apple/llvm-project/tree/apple/stable/20190104)
  was generated from the `swift-5.1-branch` branches in
  [swift-clang](https://github.com/apple/swift-clang/),
  [swift-llvm](https://github.com/apple/swift-llvm/),
  [swift-compiler-rt](https://github.com/apple/swift-compiler-rt/),
  [swift-clang-tools-extra](https://github.com/apple/swift-clang-tools-extra/),
  and [swift-libcxx](https://github.com/apple/swift-libcxx/), with the notable
  **exclusion** of [swift-lldb](https://github.com/apple/swift-lldb/),
- [swift/swift-5.1-branch](https://github.com/apple/llvm-project/tree/swift/swift-5.1-branch)
  was generated from the `swift-5.1-branch` branch in
  [swift-lldb](https://github.com/apple/swift-lldb/), interleaved with merges
  from
  [apple/stable/20190104](https://github.com/apple/llvm-project/tree/apple/stable/20190104).
- [swift/master](https://github.com/apple/llvm-project/tree/swift/master) was
  generated from the `stable` branch from all six split repos.

## Workflow tips for LLDB engineers

### Swift content

The `swift/master` branch in the monorepo contains the LLDB content from the `stable` branch in the split [github.com/apple/swift-lldb](https://github.com/apple/swift-lldb) repository.

The `swift/master-next` branch in the monorepo contains the LLDB content from the `upstream-with-swift` branch in the split [github.com/apple/swift-lldb](https://github.com/apple/swift-lldb) repository.

### Non-Swift content

The `apple/master` branch should contain no differences from the `llvm.org/master` branch. It shouldn't be used for LLDB work. If you need to commit something to `apple/master`, it should go to `master` at [github.com/llvm/llvm-project](https://github.com/llvm/llvm-project) instead!
