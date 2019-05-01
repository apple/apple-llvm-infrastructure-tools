// commit_source.h
#pragma once

#include "git_cache.h"
#include <atomic>
#include <optional>
#include <thread>
#include <vector>

namespace {
struct index_range {
  int first = -1;
  unsigned count = 0;
};

struct monocommit_future {
  sha1_ref commit;
  const char *tree = nullptr;
  bool has_error = false;
};

struct monocommit_worker {
  /// Processed in "stack" ordering, rback first.
  std::vector<monocommit_future> futures;

  /// When set to 0, all futures are ready.
  std::atomic<int> last_ready_future = -1;

  /// Returns after spawning a thread to process all the futures.
  void start() {
    thread.emplace([&]() { process_futures(); });
  }
  std::optional<std::thread> thread;

private:
  void process_futures();
  bump_allocator alloc;
};

struct commit_source {
  index_range commits;
  int dir_index = -1;
  bool is_root = false;

  std::unique_ptr<monocommit_worker> worker;
};
} // end namespace

void monocommit_worker::process_futures() {
  auto processed = futures.end();
  last_ready_future = futures.size();
  assert(false && "not implemented");
  (void)processed;
}
