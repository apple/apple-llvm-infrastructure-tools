// commit_source.h
#pragma once

#include "error.h"
#include "git_cache.h"
#include "parsers.h"
#include <atomic>
#include <optional>
#include <string>
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
  int head_p = -1;
  bool has_parents = false;
  bool is_merge = false;
  bool is_translated = false;
  bool is_locked_in = false;

  fparent_type() = delete;
  explicit fparent_type(int index) : index(index) {}
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
  int source_index = -1;
  int dir_index = -1;
  bool has_root = false;
  bool is_repeat = false;
  bool extra_commits_have_been_translated = false;
  sha1_ref first_repeat_first_parent;
  sha1_ref &head;
  sha1_ref goal;
  sha1_ref cmdline_start;

  bool has_changed_head() const {
    return cmdline_start && head != cmdline_start;
  }

  // Stored here temporarily.
  std::vector<fparent_type> fparents;
  int num_fparents_to_translate = 0;
  int num_fparents_from_start = -1;
  long long first_untranslated_ct = LLONG_MAX;

  std::unique_ptr<monocommit_worker> worker;

  commit_source(int source_index, dir_type &dir, int dir_index)
      : source_index(source_index), dir_index(dir_index), has_root(dir.is_root),
        head(dir.head), goal(dir.head), cmdline_start(dir.head) {}
  commit_source(int source_index, sha1_ref &head)
      : source_index(source_index), is_repeat(true), head(head), goal(head),
        cmdline_start(head) {}

  int clean_head(git_cache &cache);
  void lock_in_start_dir_commits();
  int find_dir_commits_to_match_and_update_head(git_cache &cache,
                                                const std::string &since);
  int find_dir_commits(git_cache &cache);
  int find_repeat_commits_and_head(git_cache &cache, long long earliest_ct);
  int find_repeat_commits_and_head_impl(git_cache &cache, long long earliest_ct,
                                        std::vector<const char *> &argv,
                                        sha1_ref &next);
  int find_repeat_head(git_cache &cache, sha1_ref descendent);
  int find_earliest_ct(git_cache &cache, const std::vector<sha1_ref> &sha1s,
                       long long &earliest_ct);
  int skip_repeat_commits();

  int add_repeat_search_names(git_cache &cache, sha1_ref start,
                              std::vector<const char *> &argv);

  int list_first_ancestry_path(git_cache &cache);
  int list_first_parents(git_cache &cache);
  int list_first_parents_limit(git_cache &cache, int limit);
  int list_first_parents_limit(git_cache &cache, const std::string &limitter);
  int list_first_parents_limit_impl(
      git_cache &cache, const std::string &limitter, std::string start,
      sha1_ref &last_first_parent,
      std::vector<sha1_ref> stops = std::vector<sha1_ref>());
  int get_next_fparent(git_cache &cache, sha1_ref &sha1);
  static int get_next_fparent_impl(const fparent_type &fparent,
                                   git_cache &cache, sha1_ref &sha1);
  void validate_last_ct();

  int find_dir_commit_parents_to_translate(
      git_cache &cache, bump_allocator &parent_alloc,
      std::vector<commit_type> &untranslated);
  int extract_mtsplits(git_cache &cache, std::vector<std::string> &mtsplits);
  int queue_boundary_commit(git_cache &cache, sha1_ref commit);
  int parse_boundary_metadata(git_cache &cache, sha1_ref commit,
                              const char *&current, const char *end);
  int parse_untranslated_commit(git_cache &cache, bump_allocator &parent_alloc,
                                sha1_ref commit, const char *&current,
                                const char *end, commit_type &untranslated,
                                std::vector<sha1_ref> &parents,
                                bool &should_skip);
  int parse_dir_metadata(git_cache &cache, sha1_ref commit,
                         const char *&current, const char *end);
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

template <class I, class P>
static I find_first_match(I first, I last, P pred) {
  if (first == last)
    return last;

  I mid = first + (last - first) / 2;
  if (pred(*mid))
    return find_first_match(first, mid, pred);
  return find_first_match(mid + 1, last, pred);
}

int commit_source::clean_head(git_cache &cache) {
  assert(goal);
  assert(head);
  if (head == goal)
    return 0;

  sha1_ref base;
  if (cache.merge_base(head, goal, base))
    return error("failed to find merge base between head and goal");
  assert(base);

  if (head == base)
    return 0;

  // Update head to the merge base, or discard it if this is a repeat.
  if (is_repeat)
    head = sha1_ref();
  else
    cache.dirs.set_head(dir_index, base);
  return 0;
}

void commit_source::lock_in_start_dir_commits() {
  assert(head);
  assert(num_fparents_from_start != -1);

  // Okay, do it.
  assert(fparents.size() >= size_t(num_fparents_from_start));
  for (int i = 0, ie = num_fparents_from_start; i != ie; ++i)
    fparents[i].is_locked_in = true;
  if (fparents.size() == size_t(num_fparents_from_start))
    return;

  // Drop extra commits.
  assert(head == fparents[num_fparents_from_start].commit);
  fparents.erase(fparents.begin() + num_fparents_from_start, fparents.end());
}

int commit_source::find_dir_commits_to_match_and_update_head(
    git_cache &cache, const std::string &since) {
  assert(!is_repeat);

  // Otherwise, extend into already translated commits to match the most
  // recent untranslated one.
  sha1_ref start;
  if (get_next_fparent(cache, start))
    return 1;
  if (!start)
    return 0;

  sha1_ref last_first_parent;
  if (list_first_parents_limit_impl(cache, since, start->to_string(),
                                    last_first_parent))
    return 1;

  if (last_first_parent) {
    sha1_ref mono;
    assert(!cache.compute_mono(last_first_parent, mono));
    cache.dirs.set_head(dir_index, last_first_parent);
  }
  return 0;
}

// We will interleave first parent commits, sorting by commit timestamp,
// putting the earliest at the back of the vector and top of the stack.  Use
// stable sort to prevent reordering within a source.
static bool by_non_increasing_commit_timestamp(const fparent_type &lhs,
                                               const fparent_type &rhs) {
  if (lhs.ct > rhs.ct)
    return true;
  if (lhs.ct < rhs.ct)
    return false;
  // Put repeats at the back, to pop off first.
  return lhs.index > rhs.index;
}

int commit_source::find_dir_commits(git_cache &cache) {
  assert(!is_repeat);

  // List commits.
  if (head) {
    if (list_first_ancestry_path(cache))
      return error("failed to list ancestry path");
  } else {
    if (list_first_parents(cache))
      return error("failed to list first parents");
  }

  // Figure out how many need to be translated.
  auto translated =
      find_first_match(fparents.begin(), fparents.end(), [&](fparent_type &fp) {
        sha1_ref mono;
        return !cache.compute_mono(fp.commit, mono);
      });
  num_fparents_to_translate = translated - fparents.begin();
  if (num_fparents_to_translate)
    first_untranslated_ct = translated[-1].ct;

  // Mark translated commits, and ensure anything else we pull in is marked
  // that way too.
  extra_commits_have_been_translated = true;
  for (auto fp = fparents.begin() + num_fparents_to_translate,
            fpe = fparents.end();
       fp != fpe; ++fp) {
    fp->is_translated = true;
  }

  // We can assert here since parse_source is supposed to fudge any
  // inconsistencies so that sorting later is legal.
  assert(std::is_sorted(fparents.begin(), fparents.end(),
                        by_non_increasing_commit_timestamp));
  return 0;
}

int commit_source::list_first_parents(git_cache &cache) {
  // Parse 1000 at a time.
  constexpr const int limit = 1000;

  size_t prev_fparents_size = fparents.size();
  while (true) {
    if (list_first_parents_limit(cache, limit))
      return 1;

    if (prev_fparents_size == fparents.size())
      break;
    prev_fparents_size = fparents.size();

    if (!fparents.back().has_parents)
      break;

    sha1_ref mono;
    if (!cache.compute_mono(fparents.back().commit, mono))
      break;
  }
  return 0;
}

int commit_source::list_first_parents_limit(git_cache &cache, int limit) {
  assert(limit > 0);
  return list_first_parents_limit(cache, "-" + std::to_string(limit));
}

int commit_source::get_next_fparent(git_cache &cache, sha1_ref &sha1) {
  if (fparents.empty()) {
    assert(goal);
    sha1 = goal;
    return 0;
  }
  return get_next_fparent_impl(fparents.back(), cache, sha1);
}

int commit_source::get_next_fparent_impl(const fparent_type &fparent,
                                         git_cache &cache, sha1_ref &sha1) {
  //  No parents.
  sha1 = sha1_ref();
  if (fparent.head_p == -1)
    return 0;

  // Note: head_p counts from 0, but the first parent is 1 in Git.
  std::string rev = fparent.commit->to_string() + "^" +
                    std::to_string(fparent.head_p + 1);
  if (cache.rev_parse(rev, sha1))
    return error("failed to parse rev " + rev);
  return 0;
}

int commit_source::list_first_parents_limit(git_cache &cache,
                                            const std::string &limitter) {
  sha1_ref last_first_parent;
  sha1_ref start;
  if (get_next_fparent(cache, start))
    return 1;
  if (!start)
    return 0;

  return list_first_parents_limit_impl(cache, limitter, start->to_string(),
                                       last_first_parent);
}

int commit_source::list_first_parents_limit_impl(git_cache &cache,
                                                 const std::string &limitter,
                                                 std::string start,
                                                 sha1_ref &last_first_parent,
                                                 std::vector<sha1_ref> stops) {
  assert(!start.empty());

  // Repeat fparents don't have the right metadata to make this work.
  assert(!is_repeat);

  auto &git_reply = cache.git_reply;
  std::vector<const char *> argv = {
      "git",
      "log",
      "--first-parent",
      "--date=raw",
      "--format=tformat:%H %ct %P%x00%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B%x00",
      start.c_str(),
  };
  if (!limitter.empty())
    argv.push_back(limitter.c_str());
  std::vector<std::string> stop_sha1s;
  if (!stops.empty()) {
    argv.push_back("--not");
    for (auto &stop : stops)
      stop_sha1s.push_back(stop->to_string());
    for (auto &stop : stop_sha1s)
      argv.push_back(stop.c_str());
  }
  argv.push_back(nullptr);
  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return error("git failed");
  git_reply.push_back(0);

  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    fparents.emplace_back(source_index);
    fparents.back().is_translated = extra_commits_have_been_translated;
    if (cache.pool.parse_sha1(current, fparents.back().commit) ||
        parse_space(current) || parse_ct(current, fparents.back().ct) ||
        parse_space(current))
      return error("failed to parse commit and ct");
    validate_last_ct();

    last_first_parent = sha1_ref();
    const char *metadata = current;
    const char *end_metadata = end;
    if (cache.parse_for_store_metadata(fparents.back().commit, metadata,
                                       end_metadata, fparents.back().is_merge,
                                       last_first_parent))
      return 1;
    cache.store_metadata_if_new(fparents.back().commit, metadata, end_metadata,
                                fparents.back().is_merge, last_first_parent);
    current = end_metadata;
    if (last_first_parent) {
      fparents.back().has_parents = true;
      fparents.back().head_p = 0;
    }
    if (parse_null(current) || parse_newline(current))
      return 1;
  }
  return 0;
}

int commit_source::list_first_ancestry_path(git_cache &cache) {
  // The path is empty.
  if (head == goal) {
    num_fparents_from_start = 0;
    return 0;
  }

  assert(!extra_commits_have_been_translated);
  std::string start = goal->to_string();
  std::string stop = head->to_string();
  auto &git_reply = cache.git_reply;
  const char *argv[] = {
      "git",
      "log",
      "--format=tformat:%H %ct %P",
      "--ancestry-path",
      start.c_str(),
      "--not",
      stop.c_str(),
      nullptr,
  };
  git_reply.clear();
  if (call_git(argv, nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  struct ancestry_node {
    sha1_ref commit;
    long long ct = -1;
    const char *parents = nullptr;
  };

  std::vector<ancestry_node> ancestry;
  auto in_ancestry = std::make_unique<sha1_trie<git_cache::sha1_single>>();
  bool was_inserted;
  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    ancestry.emplace_back();
    if (cache.pool.parse_sha1(current, ancestry.back().commit) ||
        parse_space(current) || parse_ct(current, ancestry.back().ct) ||
        parse_space(current))
      return 1;
    in_ancestry->insert(*ancestry.back().commit, was_inserted);
    ancestry.back().parents = current;
    if (parse_through_newline(current))
      return 1;
  }

  auto included = std::make_unique<sha1_trie<git_cache::sha1_single>>();
  in_ancestry->insert(*head, was_inserted);
  included->insert(*goal, was_inserted);
  for (auto &an : ancestry) {
    if (!included->lookup(*an.commit))
      continue;

    // We don't care much about the parents, we're just storing the metadata.
    fparents.emplace_back(source_index);
    fparents.back().commit = an.commit;
    fparents.back().ct = an.ct;
    fparents.back().has_parents = true;
    validate_last_ct();

    // Should always have at least one parent.
    const char *current = an.parents;
    int p = 0;
    auto handle_parent = [&]() {
      if (p == 1)
        fparents.back().is_merge = true;
      sha1_ref parent;
      if (cache.pool.parse_sha1(current, parent))
        return 1;
      if (fparents.back().head_p != -1)
        return 0;
      if (!in_ancestry->lookup(*parent))
        return 0;
      fparents.back().head_p = p;
      included->insert(*parent, was_inserted);
      return 0;
    };
    if (handle_parent())
      return error("failed to parse first parent in ancestry path");
    for (++p; !parse_space(current); ++p)
      if (handle_parent())
        return error("failed to parse parent in ancestry path");
    if (*current != '\n')
      return error("failed to parse parents in ancestry path");
    if (fparents.back().head_p == -1)
      return error("failed to traverse ancestry path");
  }

  num_fparents_from_start = fparents.size();
  return 0;
}

void commit_source::validate_last_ct() {
  if (fparents.size() < 2)
    return;
  auto fp = fparents.rbegin();
  if (fp[0].ct <= fp[1].ct)
    return;

  // Fudge commit timestamp for future sorting purposes, ensuring that fparents
  // is monotonically non-increasing by commit timestamp within each source.
  // We should never get here since (a) these are seconds since epoch in UTC
  // and (b) they get updated on rebase.  However, in theory a committer could
  // have significant clock skew.
  fprintf(
      stderr,
      "warning: apparent clock skew in %s\n"
      "   note: ancestor %s has earlier commit timestamp\n"
      "   note: using ancestor timestamp %lld instead of %lld for sorting\n",
      fp[0].commit->to_string().c_str(), fp[1].commit->to_string().c_str(),
      fp[1].ct, fp[0].ct);
  fp[0].ct = fp[1].ct;
}

int commit_source::skip_repeat_commits() {
  assert(is_repeat);
  assert(head == goal);
  num_fparents_from_start = 0;
  return 0;
}

int commit_source::find_earliest_ct(git_cache &cache,
                                    const std::vector<sha1_ref> &sha1s,
                                    long long &earliest_ct) {
  assert(!sha1s.empty());
  earliest_ct = LLONG_MAX;

  auto &git_reply = cache.git_reply;
  std::vector<const char *> argv = {
      "git",
      "log",
      "--format=%ct",
      "--no-walk",
  };
  std::vector<std::string> strings;
  for (auto &sha1 : sha1s)
    strings.push_back(sha1->to_string());
  for (auto &sha1 : strings)
    argv.push_back(sha1.c_str());
  argv.push_back(nullptr);
  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    long long ct;
    if (parse_num(current, ct) || parse_newline(current))
      return error("failed to parse commit timestamp");
    earliest_ct = std::min(ct, earliest_ct);
  }
  return 0;
}

int commit_source::find_repeat_commits_and_head(git_cache &cache,
                                                long long earliest_ct) {
  assert(is_repeat);
  assert(head != goal && "logic error, should have skipped instead");
  assert(goal);

  // Repeats don't use this flag.
  assert(!extra_commits_have_been_translated);

  std::string start_sha1 = goal->to_string();
  std::vector<const char *> argv = {
      "git",
      "log",
      "--first-parent",
      "--date=raw",
      "--format=%x01%H %ct %P%x00%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B%x00",
      "-1000",
      start_sha1.c_str(),
  };
  int start_index = argv.size() - 1;
  std::string stop;
  if (head) {
    stop = head->to_string();
    argv.push_back("--not");
    argv.push_back(stop.c_str());
  }
  argv.push_back("--");
  if (add_repeat_search_names(cache, goal, argv))
    return error("failed to add search terms for repeat head");
  argv.push_back(nullptr);

  sha1_ref next;
  while (!find_repeat_commits_and_head_impl(cache, earliest_ct, argv, next)) {
    if (next) {
      start_sha1 = next->to_string();
      argv[start_index] = start_sha1.c_str();
      continue;
    }

    if (fparents.empty())
      return 0;
    assert(std::is_sorted(fparents.begin(), fparents.end(),
                          by_non_increasing_commit_timestamp));

    // Refine the repeat goal.
    goal = fparents.front().commit;
    return 0;
  }

  return error("failed to find repeat commits");
}

int commit_source::find_repeat_commits_and_head_impl(
    git_cache &cache, long long earliest_ct,
    std::vector<const char *> &argv, sha1_ref &next) {
  auto &git_reply = cache.git_reply;
  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  // Unset "next", in case there are no search results.
  next = sha1_ref();

  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    fparents.emplace_back(source_index);
    if (parse_ch(current, 1) ||
        cache.pool.parse_sha1(current, fparents.back().commit) ||
        parse_space(current) || parse_num(current, fparents.back().ct) ||
        parse_space(current))
      return error("failed to parse repeat commit");
    long long real_ct = fparents.back().ct;
    validate_last_ct();

    // Reinitialize "next"; it'll be left unset if there's no first-parent.
    next = sha1_ref();

    // Save the metadata.
    const char *metadata = current;
    const char *end_metadata = end;
    if (cache.parse_for_store_metadata(fparents.back().commit, metadata,
                                       end_metadata, fparents.back().is_merge,
                                       next))
      return error("failed to parse metadata in repeat commit '" +
                   fparents.back().commit->to_string() + "'");
    cache.store_metadata_if_new(fparents.back().commit, metadata, end_metadata,
                                fparents.back().is_merge,
                                next);
    current = end_metadata;
    if (parse_null(current) || parse_newline(current))
      return error("missing terminator for repeat commit '" +
                   fparents.back().commit->to_string() + "'");

    // Break once we're one past the earliest other commit timestamp.
    if (earliest_ct < LLONG_MAX && real_ct < earliest_ct) {
      // Rewind the search by one and set the head if it's not already set.
      if (!head)
        head = fparents.back().commit;
      fparents.pop_back();
      next = sha1_ref();
      return 0;
    }

    if (!next)
      return 0;

    // Point out which parent to override.
    fparents.back().head_p = 0;
    fparents.back().has_parents = true;
  }

  return 0;
}

int commit_source::add_repeat_search_names(git_cache &cache, sha1_ref start,
                                           std::vector<const char *> &argv) {
  assert(is_repeat);
  assert(start);

  // Be approximate, and look only for changes to currently known paths.  This
  // will miss deletions but that's fine.  The goal will ensure the final
  // content is correct anyway.
  git_tree tree;
  tree.sha1 = start;
  if (cache.ls_tree(tree))
    return error("could not ls-tree repeat '" + start->to_string() + "'");
  for (int i = 0, ie = tree.num_items; i != ie; ++i) {
    const char *name = tree.items[i].name;
    int d = cache.dirs.find_dir(name);
    if (d == -1)
      return error("unexpected root item in '" + start->to_string() + "'");
    if (cache.dirs.repeated_dirs.test(d))
      argv.push_back(name);
  }
  return 0;
}

int commit_source::find_repeat_head(git_cache &cache, sha1_ref descendent) {
  assert(is_repeat);
  assert(descendent);
  assert(!head);

  // Note: should be a rev-parse here, in case descendent has no parents.
  // That's effectively impossible since the repeated branches will always be
  // downstream of llvm.org, and we're not going to a have a goal commit of
  // r1... except tests care.  If the rev-parse fails, that's okay.
  std::string start = descendent->to_string();
  std::vector<const char *> argv = {
      "git", "rev-list", "-2", "-m", "--first-parent", start.c_str(), "--",
  };
  if (add_repeat_search_names(cache, descendent, argv))
    return error("failed to add search terms for repeat head");
  argv.push_back(nullptr);

  auto &git_reply = cache.git_reply;
  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  // There will be 1 or 2 commits.  Either is fine.
  const char *current = git_reply.data();
  (void)cache.pool.parse_sha1(current, head);
  return 0;
}

int commit_source::extract_mtsplits(git_cache &cache,
                                    std::vector<std::string> &mtsplits) {
  assert(!is_repeat);

  // Even if this is a discovered head (without an mt-split ref), we know it
  // has already been translated.
  if (head)
    mtsplits.push_back(head->to_string());

  // Look up all refs <dir>/mt-split.
  std::string mtsplit_suffix = cache.dirs.list[dir_index].name;
  mtsplit_suffix.append("/mt-split");
  const char *argv[] = {
      "git",
      "show-ref",
      mtsplit_suffix.c_str(),
      nullptr,
  };

  auto &git_reply = cache.git_reply;
  git_reply.clear();
  if (call_git(argv, nullptr, "", git_reply, /*ignore_errors=*/true))
    return 0;
  git_reply.push_back(0);

  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    sha1_ref sha1;
    if (cache.pool.parse_sha1(current, sha1) || parse_space(current) ||
        parse_through_newline(current))
      return 1;
    mtsplits.push_back(sha1->to_string());
  }

  return 0;
}

int commit_source::find_dir_commit_parents_to_translate(
    git_cache &cache, bump_allocator &parent_alloc,
    std::vector<commit_type> &untranslated) {
  assert(!is_repeat);
  assert(goal);

  std::vector<std::string> mtsplits;
  if (extract_mtsplits(cache, mtsplits))
    return 1;

  std::string start_sha1 = goal->to_string();
  std::vector<const char *> argv = {
      "git",
      "log",
      "--reverse",
      "--date-order",
      "--date=raw",
      "--format=tformat:%m%H %T %P%x00%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B%x00",
      start_sha1.c_str(),
      "--not",
  };
  for (auto &mtsplit : mtsplits)
    argv.push_back(mtsplit.c_str());
  argv.push_back(nullptr);

  auto &git_reply = cache.git_reply;
  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  // Translate the commits.
  commits.first = untranslated.size();
  std::vector<sha1_ref> parents;
  const char *current = git_reply.data();
  const char *end = git_reply.data() + git_reply.size() - 1;
  while (current != end) {
    // line ::= ( GT | MINUS ) commit SP tree ( SP parent )*
    bool is_boundary = false;
    sha1_ref commit, tree;
    if (parse_boundary(current, is_boundary) ||
        cache.pool.parse_sha1(current, commit) || parse_space(current) ||
        cache.pool.parse_sha1(current, tree))
      return 1;

    // Warm the cache.
    assert(commit);
    assert(tree);
    cache.note_commit_tree(commit, tree);
    if (is_boundary) {
      // If this is a boundary commit, skip ahead after warming the cache.
      if (parse_boundary_metadata(cache, commit, current, end))
        return 1;
      continue;
    }

    // Parse the commit.
    bool should_skip = false;
    untranslated.emplace_back();
    untranslated.back().commit = commit;
    untranslated.back().tree = tree;
    if (parse_untranslated_commit(cache, parent_alloc, commit, current, end,
                                  untranslated.back(), parents, should_skip))
      return 1;

    // Pop off the commit if it's being skipped.
    if (should_skip)
      untranslated.pop_back();
  }

  // Store the number of commits.
  commits.count = untranslated.size() - commits.first;

  // Start the worker.
  if (worker)
    worker->start();

  return 0;
}

int commit_source::parse_boundary_metadata(git_cache &cache, sha1_ref commit,
                                           const char *&current,
                                           const char *end) {
  const char *metadata = current;
  const char *end_metadata = end;
  bool is_merge = false;
  sha1_ref first_parent;
  if (parse_space(current) ||
      cache.parse_for_store_metadata(commit, metadata, end_metadata, is_merge,
                                     first_parent))
    return error("failed to store boundary metadata for '" +
                 commit->to_string() + "'");
  cache.store_metadata_if_new(commit, metadata, end_metadata, is_merge,
                              first_parent);
  current = end_metadata;
  if (parse_null(current) || parse_newline(current))
    return error("missing newline after commit");

  return queue_boundary_commit(cache, commit);
}

int commit_source::parse_untranslated_commit(
    git_cache &cache, bump_allocator &parent_alloc, sha1_ref commit,
    const char *&current, const char *end, commit_type &untranslated,
    std::vector<sha1_ref> &parents, bool &should_skip) {
  parents.clear();
  while (!parse_space(current)) {
    // Check for a null character after the space, in case there are no
    // parents at all.
    if (!*current) {
      if (parents.empty())
        break;
      return error("expected another parent after space");
    }

    parents.emplace_back();
    if (cache.pool.parse_sha1(current, parents.back()))
      return error("failed to parse parent");

    if (!worker)
      continue;

    sha1_ref mono;
    if (cache.lookup_mono(parents.back(), mono))
      continue;
    const boundary_commit *bc = worker->boundary_index_map.lookup(*mono);
    if (!bc)
      continue;

    // Mark how long to wait.
    untranslated.has_boundary_parents = true;
    if (bc->index > untranslated.last_boundary_parent)
      untranslated.last_boundary_parent = bc->index;
  }

  if (parse_through_null(current, end))
    return error("missing null character before metadata");
  const char *metadata = current;
  if (parse_through_null(current, end))
    return error("missing null character after metadata");
  cache.store_metadata_if_new(commit, metadata, current - 1,
                              /*is_merge=*/parents.size() > 1,
                              parents.empty() ? sha1_ref() : parents.front());

  if (parse_newline(current))
    return 1;

  // Now that we have metadata (necessary for an SVN revision, if relevant),
  // check if commit has already been translated.
  sha1_ref mono;
  if (!cache.compute_mono(commit, mono)) {
    assert(mono);
    should_skip = true;
    return 0;
  }

  // We're committed to translating this commit.
  cache.note_being_translated(commit);
  untranslated.num_parents = parents.size();
  if (!parents.empty()) {
    untranslated.parents = new (parent_alloc) sha1_ref[parents.size()];
    std::copy(parents.begin(), parents.end(), untranslated.parents);
  }

  return 0;
}

int commit_source::queue_boundary_commit(git_cache &cache, sha1_ref commit) {
  if (!worker)
    worker.reset(new monocommit_worker);

  // Look up the monorepo commit.  Needs to be after noting the metadata to
  // avoid needing to shell out to git-log.
  sha1_ref mono;
  if (cache.compute_mono(commit, mono))
    return error("cannot find monorepo commit for boundary parent " +
                 commit->to_string());

  // Mark it as a boundary commit and tell the worker about it.
  bool was_inserted = false;
  boundary_commit *bc = worker->boundary_index_map.insert(*mono, was_inserted);
  if (!bc)
    return error("failure to log a commit as a monorepo commit");
  bc->index = worker->futures.size();
  worker->futures.emplace_back();
  worker->futures.back().commit = mono;

  // Get the rev.
  int rev = 0;
  if (cache.lookup_rev(commit, rev) || !rev) {
    // This can't be an upstream SVN commit or compute_mono would have
    // cached the rev.  Just check the svnbaserev table.
    if (cache.compute_base_rev(mono, rev))
      return error("cannot get rev for boundary parent " + commit->to_string());
    (void)rev;
  } else {
    // compute_mono above filled this in.  Note it in the monorepo commit
    // as well.
    //
    // TODO: add a testcase where a second-level branch needs the
    // rev from a parent on a first-level branch.
    cache.note_rev(mono, rev);
  }
  return 0;
}
