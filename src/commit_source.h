// commit_source.h
#pragma once

#include "git_cache.h"
#include <atomic>
#include <vector>

namespace {
struct index_range {
  int first = -1;
  unsigned count = 0;
};

struct monocommit_future {
  // Known.
  sha1_ref split;
  sha1_ref mono;

  // To be discovered.
  const char *metadata = nullptr;
  const char *tree = nullptr;

  /// Set to true when the metadata is filled in.
  std::atomic<bool> is_ready;

  monocommit_future() : is_ready(false) {}
  monocommit_future(const monocommit_future &x)
      : split(x.split), mono(x.mono), metadata(x.metadata), tree(x.tree),
        is_ready(bool(x.is_ready)) {}
};

struct monocommit_worker {
  /// Processed in "stack" ordering, last item first.
  std::vector<monocommit_future> futures;

  /// Track errors so main thread can.
  std::atomic<bool> has_error;

  /// For the consumer to track the next index to check whether is_ready, by
  /// comparing against commit_type::first_boundary_parent.
  int is_known_ready = -1;

  /// Returns after spawning a thread to process all the futures.
  int start();

  bump_allocator alloc;
};

struct commit_source {
  index_range commits;
  int dir_index = -1;
  bool is_root = false;

  std::unique_ptr<monocommit_worker> worker;
};
} // end namespace
