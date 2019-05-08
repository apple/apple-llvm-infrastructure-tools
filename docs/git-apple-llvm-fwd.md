# git-apple-llvm-fwd

`git apple-llvm fwd` is a tool for forwarding branches between two remotes.


## Configuration

Configuration *should* be stored in the `FETCH_HEAD` of the remote that wants
configuration, under `.apple-llvm/fwd-config`.  However that depends on having
an `.apple-llvm` directory and we don't have that where we need it.  For now,
configuration is in *this* repository inside `fwd-config`.

### Current syntax

The syntax consists of `remote` and `push` directives.

- `remote <name> <url>` sets up a remote.
- `push <name> <refspec>` causes the given refspec to be pushed to the remote
  called `<name>`.

Any line whose first column is not `remote` or `push` is ignored.  This makes
it simple to add comments and use in lit tests.

#### Example with the configuration in apple-llvm-infrastructure-tools

```
remote llvm git@github.com:llvm/llvm-project.git
remote apple git@github.com:apple/llvm-project-v0.git

push apple refs/remotes/llvm/master:refs/heads/llvm/master
push apple refs/remotes/llvm/release/*:refs/heads/llvm/release/*
```

### Desired syntax and behaviour

Once the configuration is in the destination remote, we should change the tool
to take destination remote on the command-line and look up configuration in its
`FETCH_HEAD` (after cloning, etc.).  The only change to syntax is that `push`
doesn't take a remote name, since it's set up on the command-line.

#### Example once the configuration moves to the destination repository

```
remote llvm git@github.com:llvm/llvm-project.git

push refs/remotes/llvm/master:refs/heads/llvm/master
push refs/remotes/llvm/release/*:refs/heads/llvm/release/*
```
