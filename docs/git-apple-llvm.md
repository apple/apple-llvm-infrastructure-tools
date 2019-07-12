# git-apple-llvm: Getting started with git apple-llvm

The set of `git apple-llvm` tools can be used to simplify your work on the LLVM-project monorepo, both during the transition period, and after it.

## Deploying `git apple-llvm` on your machine

Prerequisites: 
- Unix shell.
- Python 3.

You can install `git apple-llvm` by cloning the repo and running the install target:

```
git clone https://github.com/apple/apple-llvm-infrastructure-tools
cd apple-llvm-infrastructure-tools
# This will install git-apple-llvm to /usr/local/bin
sudo make install
```

Please note that C++ `mt` for generating the monorepo tools are not yet installed by the Makefile, and will not run. 


The makefile can also be used to uninstall `git apple-llvm`:

```
sudo make uninstall
```

## Transition period tools

Some tools are designed to be used during the monorepo transition:

- `git apple-llvm push`: push commits from a monorepo to the split repositories. Please see its [documentation](./git-apple-llvm-push) for more details.
