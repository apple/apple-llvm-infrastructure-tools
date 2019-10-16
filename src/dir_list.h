// dir_list.h
#pragma once

#include "bisect_first_match.h"
#include "sha1_pool.h"
#include <bitset>

namespace {
struct dir_mask {
  static constexpr const int max_size = 64;
  std::bitset<max_size> bits;

  bool any() const { return bits.any(); }
  bool test(int i) const { return bits.test(i); }
  void reset(int i) { bits.reset(i); }
  void set(int i, bool value = true) { bits.set(i, value); }

  /// Container-like insertion.
  ///
  /// Shift bits \c i and higher to make room for a new bit, like a sequence
  /// insertion.  Used in conjunction with dir_list::add_dir.
  ///
  /// \post \a test(i) returns false.
  void insert(int i) {
    // TODO: add a unit test.
    assert(i < max_size);
    if (bits.none())
      return;
    if (i == 0) {
      bits <<= 1;
      return;
    }
    auto high = bits >> i << i << 1;
    auto low = bits << (max_size - i) >> (max_size - i);
    bits = high | low;
    assert(!bits.test(i));
  }
};

struct dir_name_range {
  const char *const *first = nullptr;
  const char *const *last = nullptr;
  const char *only = nullptr;

  dir_name_range() = delete;
  explicit dir_name_range(const char *name) : only(name) {}
  explicit dir_name_range(const std::vector<const char *> &names)
      : first(names.data()), last(names.data() + names.size()) {}

  const char *const *begin() const { return only ? &only : first; }
  const char *const *end() const { return only ? &only + 1 : last; }
  bool empty() const { return begin() == end(); }
};

struct dir_type {
  const char *name = nullptr;
  sha1_ref head, goal;
  bool is_root = false;
  bool is_repeated = false;
  int source_index = -1;

  explicit dir_type(const char *name) : name(name) {}
};
struct dir_list {
  std::vector<dir_type> list;
  dir_mask active_dirs;
  dir_mask tracked_dirs;
  dir_mask repeated_dirs;

  int add_dir(const char *name, bool &is_new, int &d);
  int lookup_dir(const char *name, const char *end, bool &found) const;
  bool is_dir(const char *name) const;

  /// Return a valid insertion point for name.
  int lookup_dir(const char *name, bool &found) const {
    return lookup_dir(name, name + strlen(name), found);
  }

  /// Return the correct dir for name.
  ///
  /// Returns the dir for "-" if name is not found, or -1 if "-" is also not
  /// found.
  int find_dir(const char *name) const {
    bool found = false;
    int d = lookup_dir(name, found);
    if (found)
      return d;

    // Anything unknown is part of the monorepo root.
    d = lookup_dir("-", found);
    return found ? d : -1;
  }

  void set_head(int d, sha1_ref head) {
    list[d].head = head;
    if (head)
      active_dirs.set(d);
  }

  int parse_dir(const char *&current, int &d) const;
};
} // end namespace

int dir_list::add_dir(const char *name, bool &is_new, int &d) {
  if (!name || !*name)
    return 1;
  dir_type dir(name);
  const char *end = name;
  for (; *end; ++end) {
    if (*end >= 'a' && *end <= 'z')
      continue;
    if (*end >= 'A' && *end <= 'Z')
      continue;
    if (*end >= '0' && *end <= '9')
      continue;
    switch (*end) {
    default:
      return 1;
    case '_':
    case '-':
    case '+':
    case '.':
      continue;
    }
  }

  bool found = false;
  d = lookup_dir(name, found);
  is_new = !found;
  if (is_new) {
    if (size_t(d) != list.size()) {
      repeated_dirs.insert(d);
      tracked_dirs.insert(d);
      active_dirs.insert(d);
    }
    list.insert(list.begin() + d, dir);
  }
  if (name[0] == '-' && name[1] == 0)
    list[d].is_root = true;
  return 0;
}

bool dir_list::is_dir(const char *name) const {
  bool found = false;
  (void)lookup_dir(name, name + strlen(name), found);
  return found;
}

int dir_list::lookup_dir(const char *name, const char *end, bool &found) const {
  ptrdiff_t count = end - name;
  int d = bisect_first_match(list.begin(), list.end(),
                             [name, count](const dir_type &dir) {
                               return strncmp(name, dir.name, count) <= 0;
                             }) -
          list.begin();
  if (d == ptrdiff_t(list.size()))
    found = false;
  else
    found = !strncmp(name, list[d].name, count) && !list[d].name[count];
  return d;
}
