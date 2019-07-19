
# Apple LLVM Infrastructure Tools

This is a collection of tools for maintaining LLVM-project&ndash;related
infrastructure, including CI, automerging, monorepo transition, and others.

## Deploying `git-apple-llvm`

Prerequisites:
- Python 3
- Relatively recent git (git 2.20+ should work)

You can deploy `git-apple-llvm` by running the `install` target:

```
sudo make install                 # Installs into /usr/local/bin
make install PREFIX=/my/directory # Installs into /my/directory/bin
```

You can always uninstall the tools by running the `uninstall` target:

```
sudo make uninstall
```

## More documentation

`cd docs/ && make html && open _build/html/index.html` for more documentation.
