#!/bin/bash

if git rev-list HEAD >/dev/null 2>/dev/null; then
  TAG=$(git describe --abbrev=0 2>/dev/null)
  if [ ! -z "$TAG" ] ; then
    TAG_PREFIX="$TAG"
  else
    TAG_PREFIX="untagged-git-apple-llvm "
  fi
  HASH=$(git rev-list HEAD -n1)
  REMOTE=$(git remote get-url origin)
  echo "$TAG_PREFIX ($REMOTE $HASH)"
else
  echo "unknown version"
fi
