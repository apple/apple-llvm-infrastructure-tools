# helpers/mt-config-dirs.awk
NR == FNR && $1 == "dir"    && $2 == branch { ds[$3] = $4; where[$3] = NR; next }
NR == FNR && $1 == "repeat" && $2 == branch { repeated = $3; next }
NR == FNR && $1 == "repeat"                 { repeats[$2] = $3; next }
NR == FNR && $1 == "declare-dir"            { decls[$2] = 1; next }
NR == FNR                                   { next }

# Deal with the second file.
{
  any = 0
  for (d in ds) { any = 1; break }
  for (d in repeat_ds) { any = 1; break }
  if (!any) {
    printf "error: branch '%s' has no dir directives\n", branch >"/dev/stderr"
    exit 1
  }
}
!repeated                                   { nextfile }
FNR == 1 {
  # Gather all the directories repeated on this branch.
  active_repeats[repeated] = 1
  current = repeated
  while (current in repeats) {
    current = repeats[current]
    if (current in active_repeats) {
      print "error: loop in repeat directives involving '" current "'" \
            >"/dev/stderr"
      exit 1
    }
    active_repeats[current] = 1
  }
}
$1 != "dir"             { next }
$3 in ds                { next }
$2 in active_repeats    { repeat_ds[$3] = "%" repeated; next }
function print_active(dir, ref) {
  if (undecl) {
    if (!(d in decls))
      print d | sort
    return
  }
  if (refs)
    printf "%s:", ref | sort
  printf d | sort
  if (!(d in decls)) {
    printf "error: %s:%s: undeclared dir '%s'\n", FILENAME, where[d], d \
           >"/dev/stderr"
    exit 1
  }
  printf "\n" | sort
}
function print_inactive(d) {
  if (d in ds) { return }
  if (d in repeat_ds) { return }
  if (refs)
    printf "-:" | sort
  printf d | sort
  printf "\n" | sort
}
END {
  if (refs)
    sort = "sort -t : -k 2,2"
  else
    sort = "sort"
  if (active) { for (d in ds)        print_active(d, ds[d]) }
  if (repeat) { for (d in repeat_ds) print_active(d, repeat_ds[d]) }
  if (inactive) { for (d in decls)   print_inactive(d) }
}
