# Monorepo transition guide

## Background

The LLVM project is moving to a “monorepo” at [github.com/llvm/llvm-project](https://github.com/llvm/llvm-project).
llvm, clang, clang-tools-extra, compiler-rt, and libcxx will be in the same Git repository. (more background [here](http://llvm.org/docs/Proposals/GitHubMove.html)). It's scheduled to become the canonical repository, replacing Subversion, at the next LLVM developers' meeting in October of 2019.

## Apple's transition

Apple is transitioning the LLVM project sources hosted at [github.com/apple](http://github.com/apple) to
a new llvm-project monorepo that is a downstream fork of [github.com/llvm/llvm-project](https://github.com/llvm/llvm-project).

The transition is currently ongoing. Here's the most up to date status:

- July 2019: A prototype monorepo is published on Github: [github.com/apple/llvm-project-v1](http://github.com/apple/llvm-project-v1).

## Workflow impact for the Swift compiler

The Swift compiler builds against LLVM project sources hosted at [github.com/apple](https://github.com/apple)
with histories in [github.com/apple/swift-clang/](https://github.com/apple/swift-clang/),
[github.com/apple/swift-llvm/](https://github.com/apple/swift-llvm/),
[github.com/apple/swift-compiler-rt/](https://github.com/apple/swift-compiler-rt/),
[github.com/apple/swift-clang-tools-extra/](https://github.com/apple/swift-clang-tools-extra/),
and [github.com/apple/swift-libcxx/](https://github.com/apple/swift-libcxx/)
based on git-svn mirrors of the Subversion repository.

These sources are going to be "rebased" on top of the canonical LLVM project monorepo.
The Swift compiler and the open-source toolchains will build against this new repository.

We have published a v1 prototype monorepo at [github.com/apple/llvm-project-v1](http://github.com/apple/llvm-project-v1). The ["Working with the github.com/apple/llvm-project monorepo guide"](./working-on-github-apple-llvm-project) has more details on how you can use the monorepo with Swift.
