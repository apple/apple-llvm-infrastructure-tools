// commit_source.h
#pragma once

namespace {
struct index_range {
  int first = -1;
  unsigned count = 0;
};

struct commit_source {
  index_range commits;
  int dir_index = -1;
  bool is_root = false;
};
}
