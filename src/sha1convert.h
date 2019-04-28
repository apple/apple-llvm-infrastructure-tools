// sha1convert.h
#pragma once

#include "bump_allocator.h"
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
  memcpy(bytes, sha1, 40);
  if (end)
    *end = ch;
  return 0;
}
