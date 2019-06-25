# Given a commit graph, print first parents and how many commits get merged for
# each.
#
# Input should have the format `git rev-list --parents args...` with children
# listed before parents.
#
# Output is "<sha1> <num-merged>", one line per first parent (in the same order
# as the input), where 0 indicates a commit where all parents appear somehow in
# the output.  Typically, 0 means the commit has only one parent.

BEGIN { count = 0 }

function record_extra_parent(id, p) {
  # A bigger id means a younger.  Commits should be recorded as merged with the
  # youngest first-parent that is a descendent.
  if (id > merged_into[p])
    merged_into[p] = id
}

# Check for known first-parents and unseen commits.  If this commit hasn't been
# seen, it must be a head, and implicitly first-parent.
firsts[$1] || !merged_into[$1] {
  # Save the original order for printing at the end.
  count = count + 1
  order[count] = $1

  # Assume this does not bring in any commits.
  num_merged[count] = 0

  # Index this commit's parents.
  if (NF >= 2)
    firsts[$2] = 1
  for (i = 3; i <= NF; i = i + 1)
    record_extra_parent(count, $i)
  next
}

{
  # Not a first-parent.  Figure out which first-parent commit pulled this in.
  first = merged_into[$1]
  num_merged[first] = num_merged[first] + 1

  # Index this commit's parents.
  for (i = 2; i <= NF; i = i + 1)
    record_extra_parent(first, $i)
}

END {
  for (i = 1; i <= count; i = i + 1)
    print order[i], num_merged[i]
}
