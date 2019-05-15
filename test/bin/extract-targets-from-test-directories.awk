# extract-targets-from-test-directories.awk
# input: a/b/c
# output: a/b/c a/b a
{
  # Print the input.
  print $1

  # Look for the last slash.
  i = match($1, /[^\/]+\/$/)
  while (i) {
    # Print the dir.
    $1 = substr($1, 1, i-1)
    print $1

    # Look for the next (from last) slash.
    i = match($1, /[^\/]+\/$/)
  }
}
