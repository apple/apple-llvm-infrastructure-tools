// sha1convert.h
#pragma once

#include "bump_allocator.h"
#include <bitset>
#include <cassert>
#include <cstring>
#include <string>

static unsigned char convert(int ch) {
  switch (ch) {
  default:
    __builtin_unreachable();
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    return ch - '0';
  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
    return ch - 'a' + 10;
  }
}
static int unconvert(unsigned char ch, int index) {
  unsigned char stripped = index ? ch & 0xf : ch >> 4;
  assert(stripped >= 0);
  assert(stripped < 16);
  // TODO: Add a testcase for a-f.
  return stripped < 10 ? '0' + stripped : 'a' + (stripped - 10);
}

/// Return 0 (success) except for the "empty" hash (all 0s).
static int sha1tobin(unsigned char *bin, const char *text) {
  bool any = false;
  for (int i = 0; i < 40; i += 2)
    any |= bin[i / 2] = (convert(text[i]) << 4) | convert(text[i + 1]);
  return any ? 0 : 1;
}
/// Return 0 (success) except for the "empty" hash (all 0s).
static int bintosha1(char *text, const unsigned char *bin) {
  bool any = false;
  for (int i = 0; i < 40; ++i) {
    text[i] = unconvert(bin[i / 2], i % 2);
    any |= text[i] != '0';
  }
  text[40] = '\0';
  return any ? 0 : 1;
}

namespace {
struct binary_sha1 {
  unsigned char bytes[20] = {0};
  static binary_sha1 make_from_binary(const unsigned char *sha1) {
    binary_sha1 bin;
    bin.from_binary(sha1);
    return bin;
  }
  void from_binary(const unsigned char *sha1) { std::memcpy(bytes, sha1, 20); }
  int from_textual(const char *sha1) { return sha1tobin(bytes, sha1); }
  unsigned get_bits(int start, int count) const;
  int get_mismatched_bit(const binary_sha1 &x) const;
  friend bool operator==(const binary_sha1 &lhs, const binary_sha1 &rhs) {
    return !memcmp(lhs.bytes, rhs.bytes, 20);
  }
  std::string to_string() const;
};
struct textual_sha1 {
  char bytes[41] = {0};
  int from_binary(const unsigned char *sha1) { return bintosha1(bytes, sha1); }
  int from_input(const char *sha1, const char **end = nullptr);
  textual_sha1() = default;
  explicit textual_sha1(const binary_sha1 &bin) { from_binary(bin.bytes); }
  explicit operator binary_sha1() const {
    binary_sha1 bin;
    bin.from_textual(bytes);
    return bin;
  }
  std::string to_string() const;
};
} // end namespace

std::string textual_sha1::to_string() const { return bytes; }
std::string binary_sha1::to_string() const {
  return textual_sha1(*this).to_string();
}

unsigned binary_sha1::get_bits(int start, int count) const {
  assert(count > 0);
  assert(count <= 32);
  assert(start >= 0);
  assert(start <= 159);
  assert(start + count <= 160);

  // Use unsigned char to avoid weird promitions.
  // TODO: add a test where this matters.
  int index = 0;
  if (start >= 8) {
    index += start / 8;
    start = start % 8;
  }
  int totake = count + start; // could be more than 32
  unsigned long long bits = 0;
  while (totake > 0) {
    bits <<= 8;
    bits |= bytes[index++];
    totake -= 8;
  }
  if (totake < 0)
    bits >>= -totake;
  bits &= (1ull << count) - 1;
  return bits;
}

int binary_sha1::get_mismatched_bit(const binary_sha1 &x) const {
  int i = 0;
  for (; i < 160; i += 8) {
    int byte_i = i / 8;
    if (bytes[byte_i] != x.bytes[byte_i])
      break;
  }
  if (i == 160)
    return i;

  int byte_i = i / 8;
  unsigned lhs = bytes[byte_i];
  unsigned rhs = x.bytes[byte_i];
  unsigned mismatch = lhs ^ rhs;
  assert(mismatch & 0x000000ff);
  assert(!(mismatch & 0xffffff00));
  mismatch <<= 1;
  while (!(mismatch & 0x100)) {
    ++i;
    mismatch <<= 1;
  }
  return i;
}

int textual_sha1::from_input(const char *sha1, const char **end) {
  const char *ch = sha1;
  for (; *ch; ++ch) {
    // Allow "[0-9a-z]".
    if (*ch >= '0' && *ch <= '9')
      continue;
    if (*ch >= 'a' && *ch <= 'z')
      continue;

    // Hit an invalid character.  That's okay if the caller is parsing a longer
    // string.
    if (end)
      break;
    else
      return 1;
  }
  if (ch - sha1 != 40)
    return 1;
  strncpy(bytes, sha1, 41);
  if (end)
    *end = ch;
  return 0;
}

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
