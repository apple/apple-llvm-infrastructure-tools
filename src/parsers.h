// parsers.h
#pragma once

static int parse_string(const char *&current, const char *s) {
  for (; *current && *s; ++current, ++s)
    if (*current != *s)
      return 1;
  return *s ? 1 : 0;
}

static int try_parse_string(const char *&current, const char *s) {
  const char *temp = current;
  if (parse_string(temp, s))
    return 1;
  current = temp;
  return 0;
}

static int parse_ch(const char *&current, int ch) {
  if (*current != ch)
    return 1;
  ++current;
  return 0;
};

static int parse_null(const char *&current) { return parse_ch(current, 0); }
static int parse_space(const char *&current) { return parse_ch(current, ' '); }
static int parse_newline(const char *&current) {
  return parse_ch(current, '\n');
}

static int skip_until(const char *&current, int ch) {
  for (; *current; ++current)
    if (*current == ch)
      return 0;
  return 1;
}

static void skip_until_null(const char *&current) {
  while (*current)
    ++current;
}

static int parse_through_ch(const char *&current, int ch) {
  if (skip_until(current, ch))
    return 1;
  ++current;
  return 0;
}

static int parse_through_newline(const char *&current) {
  return parse_through_ch(current, '\n');
}

static int parse_through_null(const char *&current, const char *end) {
  skip_until_null(current);
  if (current == end)
    return 1;
  ++current;
  return 0;
}

static int parse_ct(const char *&current, long long &ct) {
  char *end = nullptr;
  ct = strtol(current, &end, 10);
  if (end == current || ct < 0)
    return 1;
  current = end;
  return 0;
}

static int parse_boundary(const char *&current, bool &is_boundary) {
  switch (*current) {
  default:
    return 1;
  case '-':
    is_boundary = true;
  case '>':
    break;
  }
  ++current;
  return 0;
}

static int parse_num(const char *&current, unsigned long long &num) {
  char *end = nullptr;
  unsigned long long parsed_num = strtoull(current, &end, 10);
  if (current == end)
    return 1;
  current = end;
  num = parsed_num;
  return 0;
}

static int parse_num(const char *&current, long long &num) {
  char *end = nullptr;
  long long parsed_num = strtoll(current, &end, 10);
  if (current == end)
    return 1;
  current = end;
  num = parsed_num;
  return 0;
}

static int parse_num(const char *&current, int &num) {
  const char *temp = current;
  long long parsed;
  if (parse_num(temp, parsed))
    return 1;
  if (parsed > INT_MAX || parsed < INT_MIN)
    return 1;
  current = temp;
  num = parsed;
  return 0;
}
