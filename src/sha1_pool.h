// sha1_pool.h
#pragma once

#include "bump_allocator.h"
#include "sha1convert.h"
#include <bitset>

namespace {
template <class T> struct sha1_trie {
  static_assert(sizeof(void *) == 8);
  struct subtrie_type;
  struct entry_type {
    uintptr_t data = 0;
    bool is_subtrie() const { return data & 1; }
    subtrie_type *as_subtrie() const {
      assert(is_subtrie());
      return reinterpret_cast<subtrie_type *>(data & ~uintptr_t(1));
    }
    T *as_data() const {
      assert(!is_subtrie());
      return reinterpret_cast<T *>(data & ~uintptr_t(1));
    }

    static entry_type make_subtrie(subtrie_type &st) {
      entry_type e;
      e.data = reinterpret_cast<uintptr_t>(&st) | 1;
      return e;
    }
    static entry_type make_data(const T &st) {
      entry_type e;
      e.data = reinterpret_cast<uintptr_t>(&st);
      return e;
    }
  };

  static constexpr const long num_root_bits = 12;
  struct {
    std::bitset<1 << num_root_bits> mask;
    entry_type entries[1 << num_root_bits];
  } root;
  bump_allocator subtrie_alloc;
  bump_allocator value_alloc;

  T *insert(const binary_sha1 &sha1, bool &was_inserted);
  T *lookup(const binary_sha1 &sha1) const;
  T *lookup_impl(const binary_sha1 &sha1, bool should_insert,
                 bool &was_inserted);

  bool empty() const { return root.mask.none(); }
};
template <class T> struct sha1_trie<T>::subtrie_type {
  static constexpr const long num_bits = 6;
  std::bitset<1 << num_bits> mask;
  entry_type entries[1 << num_bits];
};

struct sha1_ref {
  const binary_sha1 *sha1 = nullptr;
  sha1_ref() = default;
  explicit sha1_ref(const binary_sha1 *sha1) : sha1(sha1) {}

  explicit operator bool() const { return sha1; }
  const binary_sha1 &operator*() const {
    assert(sha1);
    return *sha1;
  }
  const binary_sha1 *operator->() const {
    assert(sha1);
    return sha1;
  }
  bool operator==(const sha1_ref &rhs) const { return sha1 == rhs.sha1; }
  bool operator!=(const sha1_ref &rhs) const { return sha1 != rhs.sha1; }
};
struct sha1_pool {
  sha1_trie<binary_sha1> root;

  sha1_ref lookup(const textual_sha1 &sha1);
  sha1_ref lookup(const binary_sha1 &sha1);
};
} // end namespace

template <class T>
T *sha1_trie<T>::insert(const binary_sha1 &sha1, bool &was_inserted) {
  return lookup_impl(sha1, true, was_inserted);
}

template <class T> T *sha1_trie<T>::lookup(const binary_sha1 &sha1) const {
  bool was_inserted;
  T *value =
      const_cast<sha1_trie *>(this)->lookup_impl(sha1, false, was_inserted);
  assert(!was_inserted);
  return value;
}

template <class T>
T *sha1_trie<T>::lookup_impl(const binary_sha1 &sha1, bool should_insert,
                             bool &was_inserted) {
  was_inserted = false;
  typedef sha1_trie::entry_type entry_type;
  entry_type *entry = nullptr;
  {
    unsigned bits = sha1.get_bits(0, sha1_trie::num_root_bits);
    if (!root.mask.test(bits)) {
      if (!should_insert)
        return nullptr;
      was_inserted = true;
      auto *value = new (value_alloc) T(sha1);
      root.mask.set(bits);
      root.entries[bits] = entry_type::make_data(*value);
      return value;
    }
    entry = &root.entries[bits];
  }

  // Check the root trie.
  typedef sha1_trie::subtrie_type subtrie_type;
  int start_bit = sha1_trie::num_root_bits;
  while (entry->is_subtrie()) {
    subtrie_type *subtrie = entry->as_subtrie();
    unsigned bits = sha1.get_bits(start_bit, sha1_trie::subtrie_type::num_bits);
    if (!subtrie->mask.test(bits)) {
      if (!should_insert)
        return nullptr;
      was_inserted = true;
      // Add an entry to the root in the empty slot.
      auto *value = new (value_alloc) T(sha1);
      subtrie->mask.set(bits);
      subtrie->entries[bits] = entry_type::make_data(*value);
      return value;
    }
    entry = &subtrie->entries[bits];
    start_bit += sha1_trie::subtrie_type::num_bits;
  }

  // Extract the existing sha1, and return it if it's the same.
  T &existing = *entry->as_data();
  const binary_sha1 &esha1 = static_cast<const binary_sha1 &>(existing);
  int first_mismatched_bit = sha1.get_mismatched_bit(esha1);
  assert(first_mismatched_bit <= 160);
  if (first_mismatched_bit == 160)
    return &existing;
  if (!should_insert)
    return nullptr;
  was_inserted = true;

  assert(first_mismatched_bit >= start_bit);
  while (first_mismatched_bit >=
         start_bit + sha1_trie::subtrie_type::num_bits) {
    // Add new subtrie.
    auto *subtrie = new (subtrie_alloc) subtrie_type;
    *entry = entry_type::make_subtrie(*subtrie);

    // Prepare for the next subtrie.
    unsigned bits = sha1.get_bits(start_bit, sha1_trie::subtrie_type::num_bits);
    subtrie->mask.set(bits);
    entry = &subtrie->entries[bits];
    start_bit += sha1_trie::subtrie_type::num_bits;
  }

  // Add final subtrie.
  auto *subtrie = new (subtrie_alloc) subtrie_type;
  *entry = entry_type::make_subtrie(*subtrie);

  // Fill it in.
  int num_bits = start_bit + sha1_trie::subtrie_type::num_bits > 160
                     ? 160 - start_bit
                     : sha1_trie::subtrie_type::num_bits;
  unsigned nbits = sha1.get_bits(start_bit, num_bits);
  unsigned ebits = esha1.get_bits(start_bit, num_bits);
  assert(nbits != ebits);

  auto *value = new (value_alloc) T(sha1);
  subtrie->mask.set(nbits);
  subtrie->mask.set(ebits);
  subtrie->entries[nbits] = entry_type::make_data(*value);
  subtrie->entries[ebits] = entry_type::make_data(existing);
  return value;
}

sha1_ref sha1_pool::lookup(const textual_sha1 &sha1) {
  // Return default-constructed for all 0s.
  binary_sha1 bin;
  if (bin.from_textual(sha1.bytes))
    return sha1_ref();
  return lookup(bin);
}

sha1_ref sha1_pool::lookup(const binary_sha1 &sha1) {
  bool was_inserted = false;
  return sha1_ref(root.insert(sha1, was_inserted));
}
