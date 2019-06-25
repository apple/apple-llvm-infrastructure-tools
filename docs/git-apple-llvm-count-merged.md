# git-apple-llvm: count-merged

`git apple-llvm count-merged` is a low-level utility for counting how many
commits get merged for each first-parent commit.

## Examples

The following will list first-parent commits between `x` and `y`, along with
how many non-first-parent commits each one merged in.

```
git apple-llvm count-merged x..y
```

The following only list the commits that merged in at least 10 commits.

```
git apple-llvm count-merged --min 10 x..y
```
