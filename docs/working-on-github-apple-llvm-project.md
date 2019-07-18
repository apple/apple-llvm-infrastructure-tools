# Working with the github.com/apple/llvm-project monorepo

This guide is mainly aimed at engineers who are using the [github.com/apple/llvm-project-v1](https://github.com/apple/llvm-project-v1)
while working on the Swift compiler.

## Using llvm-project monorepo alongside Swift

You can use Swift's `update-checkout` script to get the monorepo! The following command can be used when working with Swift's `master` branch:

```
./swift/utils/update-checkout --scheme master-llvm-monorepo --clone-with-ssh --symlink-llvm-monorepo
```

Alternatively, you can use the following command when working with Swift's `master-next` branch:

```
./swift/utils/update-checkout --scheme master-next-llvm-monorepo --clone-with-ssh --symlink-llvm-monorepo
```

## Workflow tips for LLDB engineers working with swift-lldb

### Swift content

The `swift/master` branch in the monorepo contains the LLDB content from the `stable` branch in the split [github.com/apple/swift-lldb](https://github.com/apple/swift-lldb) repository.

The `swift/master-next` branch in the monorepo contains the LLDB content from the `upstream-with-swift` branch in the split [github.com/apple/swift-lldb](https://github.com/apple/swift-lldb) repository.

Both of these branches can be used with `git apple-llvm push` to push the LLDB changes you made in the monorepo to the old split remote. For example:

```
cd llvm-project-v1
git checkout swift/master
# modify lldb
git commit -m "My swift LLDB change"
git apple-llvm push HEAD:swift/master
```

For more information on how to use `git apple-llvm push` please refer to the tool's [documentation](./git-apple-llvm-push).

### Non-Swift content

The `apple/master` branch contains the LLDB content that's equivalent to the `llvm.org/master` branch. It shouldn't be used for LLDB work. If you need to commit something to `apple/master`, it should go to `master` at [github.com/llvm/llvm-project](https://github.com/llvm/llvm-project) instead!
