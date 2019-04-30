// svnbaserev.h
#pragma once

#include <cstring>

namespace {
struct svnbaserev {
  unsigned char bytes[4] = {0};

  svnbaserev() = default;
  explicit svnbaserev(int rev) { set_rev(rev); }
  static svnbaserev make_from_binary(const unsigned char *bytes);
  int get_rev() const;
  void set_rev(int rev);
};
} // end namespace

svnbaserev svnbaserev::make_from_binary(const unsigned char *bytes) {
  svnbaserev sbr;
  std::memcpy(sbr.bytes, bytes, 4);
  return sbr;
}
int svnbaserev::get_rev() const {
  unsigned data = 0;
  data |= unsigned(bytes[0]) << 24;
  data |= unsigned(bytes[1]) << 16;
  data |= unsigned(bytes[2]) << 8;
  data |= unsigned(bytes[3]);
  return static_cast<int>(data);
}
void svnbaserev::set_rev(int rev) {
  unsigned data = static_cast<unsigned>(rev);
  bytes[0] = 0xff & (data >> 24);
  bytes[1] = 0xff & (data >> 16);
  bytes[2] = 0xff & (data >> 8);
  bytes[3] = 0xff & (data);
}
