# Given a commit graph, print first parents and how many commits get merged for
# each.
#
# Input should be as if `git rev-list --format="subject %s" --parents args...`
# with children listed before parents.
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

$1 == "subject" {
  if (order[count] == commit)
    subjects[count] = substr($0, 1 + length("subject "))
  next
}

$1 != "commit" {
  print "error: expected 'commit'" >"/dev/stderr"
  exit 1
}

{
  commit = $2
  parents_start = 3
}

# Check for known first-parents and unseen commits.  If this commit hasn't been
# seen, it must be a head, and implicitly first-parent.
firsts[commit] || !merged_into[commit] {
  # Save the original order for printing at the end.
  count = count + 1
  order[count] = commit

  # Assume this does not bring in any commits.
  num_merged[count] = 0

  # Index this commit's parents.
  if (NF >= parents_start)
    firsts[$parents_start] = 1
  for (i = parents_start + 1; i <= NF; i = i + 1)
    record_extra_parent(count, $i)
  next
}

{
  # Not a first-parent.  Figure out which first-parent commit pulled this in.
  first = merged_into[commit]
  num_merged[first] = num_merged[first] + 1

  # Index this commit's parents.
  for (i = parents_start; i <= NF; i = i + 1)
    record_extra_parent(first, $i)
}

END {
  if (width)
    format = "%s %" width "d %s\n"
  else
    format = "%s %s %s\n"
  if (unique) {
    # Merge repeated subjects.
    for (x = 1; x <= count; x = x + 1) {
      # Keep the commit hash that will be output first.
      if (reverse)
        i = count - x + 1
      else
        i = x

      subject = subjects[i]
      j = order_by_subject[subject]
      if (!j) {
        order_by_subject[subject] = i
        continue
      }

      num_merged[j] += num_merged[i]
      num_merged[i] = -1
    }
  }
  if (min < 0)
    min = 0
  for (x = 1; x <= count; x = x + 1) {
    if (reverse)
      i = count - x + 1
    else
      i = x
    if (num_merged[i] >= min)
      printf format, order[i], num_merged[i], subjects[i]
  }
}
