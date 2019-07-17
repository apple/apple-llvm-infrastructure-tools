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

struct fparent_type {
  sha1_ref commit;
  long long ct = -1;
  int index = -1;
};

struct commit_type {
  sha1_ref commit;
  sha1_ref tree;
  sha1_ref *parents = nullptr;
  int num_parents = 0;

  // Whether this commit is a generated merge commit.
  bool is_generated_merge = false;

  /// Whether this commit has parents from --boundary, which will already have
  /// monorepo equivalents.
  bool has_boundary_parents = false;

  /// Index of the first boundary parent.  Once that one is ready, the cache
  /// will be hot.
  int last_boundary_parent = -1;
};

struct boundary_commit {
  sha1_ref commit;
  int index = -1;

  explicit boundary_commit(const binary_sha1 &commit) : commit(&commit) {}
  explicit operator const binary_sha1 &() const { return *commit; }
};

struct monocommit_future {
  sha1_ref commit;
  const char *rawtree = nullptr;
  bool was_noted = false;
};

struct monocommit_worker {
  /// Processed in "stack" ordering, rback first.
  std::vector<monocommit_future> futures;

  /// When set to 0, all futures are ready.
  std::atomic<int> last_ready_future = -1;

  /// Cancel.
  std::atomic<bool> should_cancel = false;

  /// Report an error.
  std::atomic<bool> has_error = false;

  /// Returns after spawning a thread to process all the futures.
  void start() {
    thread.emplace([&]() { process_futures(); });
  }

  std::optional<std::thread> thread;
  sha1_trie<boundary_commit> boundary_index_map;

private:
  void process_futures();
  bump_allocator alloc;
  std::vector<std::unique_ptr<char[]>> big_trees;
};

struct commit_source {
  index_range commits;
  int dir_index = -1;
  bool is_root = false;
  bool is_repeat = false;
  sha1_ref first_repeat_first_parent;

  std::unique_ptr<monocommit_worker> worker;
};
} // end namespace

void monocommit_worker::process_futures() {
  std::vector<char> reply;
  for (auto fb = futures.begin(), f = fb, fe = futures.end(); f != fe; ++f) {
    if (bool(should_cancel))
      return;

    if (git_cache::ls_tree_impl(f->commit, reply)) {
      has_error = true;
      return;
    }
    assert(!reply.empty());
    assert(reply.back() == 0);

    char *storage;
    if (reply.size() > 4096) {
      big_trees.emplace_back(new char[reply.size()]);
      storage = big_trees.back().get();
    } else {
      storage = new (alloc.allocate(reply.size(), 1)) char[reply.size()];
    }
    f->rawtree = storage;
    memcpy(storage, reply.data(), reply.size());
    last_ready_future = f - fb;
  }
}
