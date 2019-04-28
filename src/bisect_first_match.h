// bisect_first_match.h
#pragma once

/// Finds the first match for comp.  Requires that the range can be bisected
/// into two (possibly empty) sub-sequences, where all the elements in the
/// first do not match and all the elements in the second do.
template <class I, class C>
static I bisect_first_match(I first, I last, C comp) {
  if (first == last)
    return first;
  I mid = first + (last - first) / 2;
  if (comp(*mid))
    return bisect_first_match(first, mid, comp);
  return bisect_first_match(++mid, last, comp);
}
