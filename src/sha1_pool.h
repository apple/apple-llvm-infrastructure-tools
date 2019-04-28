// sha1_pool.h
#pragma once

#include "bump_allocator.h"
#include "sha1convert.h"
#include <bitset>

namespace {
struct sha1_trie {
  static_assert(sizeof(void *) == 8);
  struct subtrie_type;
  struct entry_type {
    uintptr_t data = 0;
    bool is_subtrie() const { return data & 1; }
    subtrie_type *as_subtrie() const {
      assert(is_subtrie());
      return reinterpret_cast<subtrie_type *>(data & ~uintptr_t(1));
    }
    binary_sha1 *as_sha1() const {
      assert(!is_subtrie());
      return reinterpret_cast<binary_sha1 *>(data & ~uintptr_t(1));
    }

    static entry_type make_subtrie(subtrie_type &st) {
      entry_type e;
      e.data = reinterpret_cast<uintptr_t>(&st) | 1;
      return e;
    }
    static entry_type make_sha1(const binary_sha1 &st) {
      entry_type e;
      e.data = reinterpret_cast<uintptr_t>(&st);
      return e;
    }
  };

  static constexpr const long num_root_bits = 12;
  std::bitset<1 << num_root_bits> mask;
  entry_type entries[1 << num_root_bits];
};
struct sha1_trie::subtrie_type {
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
  bump_allocator subtrie_alloc;
  bump_allocator sha1_alloc;
  sha1_trie root;

  sha1_ref lookup(const textual_sha1 &sha1);
  sha1_ref lookup(const binary_sha1 &sha1);
};
} // end namespace

sha1_ref sha1_pool::lookup(const textual_sha1 &sha1) {
  // Return default-constructed for all 0s.
  binary_sha1 bin;
  if (bin.from_textual(sha1.bytes))
    return sha1_ref();
  return lookup(bin);
}
sha1_ref sha1_pool::lookup(const binary_sha1 &sha1) {
  typedef sha1_trie::entry_type entry_type;
  entry_type *entry = nullptr;
  {
    unsigned bits = sha1.get_bits(0, sha1_trie::num_root_bits);
    if (!root.mask.test(bits)) {
      binary_sha1 *ret = new (sha1_alloc) binary_sha1(sha1);
      root.mask.set(bits);
      root.entries[bits] = entry_type::make_sha1(*ret);
      return sha1_ref(ret);
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
      // Add an entry to the root in the empty slot.
      binary_sha1 *ret = new (sha1_alloc) binary_sha1(sha1);
      subtrie->mask.set(bits);
      subtrie->entries[bits] = entry_type::make_sha1(*ret);
      return sha1_ref(ret);
    }
    entry = &subtrie->entries[bits];
    start_bit += sha1_trie::subtrie_type::num_bits;
  }

  // Extract the existing sha1, and return it if it's the same.
  binary_sha1 &existing = *entry->as_sha1();
  int first_mismatched_bit = sha1.get_mismatched_bit(existing);
  assert(first_mismatched_bit <= 160);
  if (first_mismatched_bit == 160)
    return sha1_ref(&existing);

  assert(first_mismatched_bit >= start_bit);
  while (first_mismatched_bit >= start_bit + sha1_trie::subtrie_type::num_bits) {
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
  unsigned ebits = existing.get_bits(start_bit, num_bits);
  assert(nbits != ebits);

  binary_sha1 *ret = new (sha1_alloc) binary_sha1(sha1);
  subtrie->mask.set(nbits);
  subtrie->mask.set(ebits);
  subtrie->entries[nbits] = entry_type::make_sha1(*ret);
  subtrie->entries[ebits] = entry_type::make_sha1(existing);
  return sha1_ref(ret);
}
