#!/bin/bash
# Script that clones and installs HEAD of git-apple-llvm into /usr/local.

URL="$REMOTE_URL"
set +e

if [ ! -f /usr/local/bin/pip3 ] ; then
  echo "error: pip3 is missing (no python3 installed?)"
  exit 1
fi

rm -rf   /tmp/git-apple-llvm
mkdir -p /tmp/git-apple-llvm

echo "Cloning $URL"
echo "################################################################################"

(set +e; set -x;
  cd /tmp/git-apple-llvm
  git clone $URL
)

if [ -f /usr/local/bin/git-apple-llvm ] ; then
  echo ""
  echo "Running make uninstall"
  echo "################################################################################"
  (set +e; set -x;
    cd /tmp/git-apple-llvm/apple-llvm-infrastructure-tools
    sudo make uninstall PIP=/usr/local/bin/pip3
  )
fi

echo ""
echo "Running make install"
echo "################################################################################"
(set +e; set -x;
  cd /tmp/git-apple-llvm/apple-llvm-infrastructure-tools
  sudo make install PIP=/usr/local/bin/pip3
)

# Clean-up.
rm -rf /tmp/git-apple-llvm
