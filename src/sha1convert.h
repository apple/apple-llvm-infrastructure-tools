// sha1convert.h
#pragma once

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
  return stripped < 10 ? '0' + stripped : 'a' + stripped;
}

/// Return 0 (success) except for the "empty" hash (all 0s).
static int sha1tobin(unsigned char *bin, const char *text) {
  bool any = false;
  for (int i = 0; i < 40; i += 2)
    any |= bin[i / 2] = (convert(text[i]) << 4) | convert(text[i + 1]);
  bin[20] = '\0';
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
