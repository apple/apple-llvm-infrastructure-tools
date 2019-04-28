// error.h
#pragma once

#include <cstdio>
#include <string>

static int error(const std::string &msg) {
  fprintf(stderr, "error: %s\n", msg.c_str());
  return 1;
}
