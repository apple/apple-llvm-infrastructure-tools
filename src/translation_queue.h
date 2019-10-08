// translation_queue.h
#pragma once

#include "commit_source.h"
#include <deque>
#include <queue>

namespace {
struct translation_queue {
  bump_allocator parent_alloc;
  git_cache &cache;
  sha1_pool &pool;
  dir_list &dirs;
  std::deque<commit_source> sources;
  std::vector<fparent_type> fparents;
  std::vector<commit_type> commits;
  long long first_untranslated_ct = LLONG_MAX;

  explicit translation_queue(git_cache &cache, dir_list &dirs)
      : cache(cache), pool(cache.pool), dirs(dirs) {}

  void set_source_head(commit_source &source, sha1_ref sha1);

  int interleave_dir_commits();
  int ff_translated_dir_commits();
  int clean_initial_head(sha1_ref &head);
  int clean_initial_source_heads();
  int find_dir_commits(sha1_ref head);
  int find_dir_commit_parents_to_translate();
  int find_repeat_head(commit_source *repeat);
  int find_repeat_commits_and_head(commit_source *repeat);
  int interleave_repeat_commits(commit_source *repeat);
};
}

void translation_queue::set_source_head(commit_source &source, sha1_ref sha1) {
  assert(sha1);
  if (!source.is_repeat) {
    dirs.set_head(source.dir_index, sha1);
    return;
  }
  dirs.active_dirs.bits |= dirs.repeated_dirs.bits;
  source.head = sha1;
}

int translation_queue::clean_initial_source_heads() {
  for (auto &source : sources)
    if (source.head)
      if (source.clean_head(cache))
        return error("failed to clean head '" + source.head->to_string() + "'");
  return 0;
}


int translation_queue::clean_initial_head(sha1_ref &head) {
  for (auto &source : sources)
    if (source.has_changed_head()) {
      // Discard the monorepo head if any source's head has already changed.
      // Because of when this function is called, this only happens if a head
      // was discarded or rewound to a merge-base with the goal.
      head = sha1_ref();
      break;
    }
  return 0;
}

int translation_queue::find_dir_commits(sha1_ref head) {
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;

    if (source.find_dir_commits(cache))
      return error("failed to find commits for '" +
                   std::string(dirs.list[source.dir_index].name) + "'");
    first_untranslated_ct =
        std::min(first_untranslated_ct, source.first_untranslated_ct);
  }

  // Nothing to do if we have no untranslated commits.
  if (first_untranslated_ct == LLONG_MAX)
    return 0;

  std::string since = "--since=" + std::to_string(first_untranslated_ct);
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;

    // If there's a monorepo head, use the head/start commit verbatim.
    if (head) {
      // Check if we have a start commit.
      if (source.head) {
        source.lock_in_start_dir_commits();
        continue;
      }

      // No start commit is weird here, but it can happen if the mt-config file
      // is modified to add a new source directory.
      continue;
    }

    // Otherwise, extend into already translated commits to match the most
    // recent untranslated one.
    if (source.find_dir_commits_to_match_and_update_head(cache, since))
      return error("failed to find commits to match head for '" +
                   std::string(dirs.list[source.dir_index].name) + "'");
    continue;
  }

  for (auto &source : sources) {
    if (source.is_repeat)
      continue;
    if (source.list_first_parents_limit(cache, since))
      return error("failed to list first parents limit for '" +
                   std::string(dirs.list[source.dir_index].name) + "'");
  }
  return 0;
}

int translation_queue::interleave_dir_commits() {
  // Merge everything in.
  struct node {
    commit_source *source;
    node() = delete;
    explicit node(commit_source &source) : source(&source) {}

    bool operator<(const node &x) const {
      return source->fparents.size() < x.source->fparents.size();
    }
    bool operator>(const node &x) const { return !operator<(x); }
  };
  size_t total = 0;
  std::priority_queue<node, std::vector<node>, std::greater<node>> work;
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;
    if (source.fparents.empty())
      continue;

    total += source.fparents.size();
    work.emplace(source);
  }

  // Merge smallest to largest.
  std::vector<fparent_type> scratch;
  fparents.reserve(total);
  scratch.reserve(total);
  while (!work.empty()) {
    std::vector<fparent_type> x = std::move(work.top().source->fparents);
    work.pop();
    if (fparents.empty()) {
      fparents = std::move(x);
      continue;
    }
    scratch.reserve(x.size() + fparents.size());
    std::merge(x.begin(), x.end(), fparents.begin(), fparents.end(),
               std::back_inserter(scratch), by_non_increasing_commit_timestamp);
    std::swap(scratch, fparents);
    scratch.clear();
  }
  // for (auto &fp : fparents)
  //   fprintf(stderr, "ct=%llu sha1=%s\n", fp.ct, fp.commit->to_string().c_str());
  return 0;
}

int translation_queue::ff_translated_dir_commits() {
  while (!fparents.empty()) {
    // Note that we don't have any repeats yet.
    assert(!sources[fparents.back().index].is_repeat);

    if (!fparents.back().is_translated)
      break;
    if (fparents.back().is_locked_in)
      break;
    set_source_head(sources[fparents.back().index], fparents.back().commit);
    fparents.pop_back();
  }
  return 0;
}

int translation_queue::find_repeat_commits_and_head(commit_source *repeat) {
  if (!repeat)
    return 0;
  assert(repeat->is_repeat);
  if (repeat->goal == repeat->head)
    return repeat->skip_repeat_commits();

  // Try finding the earliest_ct.  It's tempting to use --since, but we
  // actually want to go back a little earlier.  See how earliest_ct is used
  // in commit_source::find_repeat_commits_and_head.
  long long earliest_ct = LLONG_MAX;
  if (!fparents.empty())
    earliest_ct = fparents.back().ct;
  std::vector<sha1_ref> monoheads;
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;
    if (!source.head)
      continue;
    monoheads.emplace_back();
    if (cache.compute_mono(source.head, monoheads.back()))
      return error("failed to lookup monorepo commit for start commit '" +
                   source.head->to_string() + "'");
  }
  {
    long long ct;
    if (!monoheads.empty())
      if (repeat->find_earliest_ct(cache, monoheads, ct))
        return 1;
    earliest_ct = std::min(earliest_ct, ct);
  }
  return repeat->find_repeat_commits_and_head(cache, earliest_ct);
}

int translation_queue::interleave_repeat_commits(commit_source *repeat) {
  if (!repeat)
    return 0;
  if (repeat->fparents.empty())
    return 0;
  if (fparents.empty()) {
    fparents = std::move(repeat->fparents);
    return 0;
  }
  std::vector<fparent_type> x = std::move(repeat->fparents);
  std::vector<fparent_type> scratch;
  scratch.reserve(x.size() + fparents.size());
  std::merge(fparents.begin(), fparents.end(), x.begin(), x.end(),
             std::back_inserter(scratch), by_non_increasing_commit_timestamp);
  fparents = std::move(scratch);
  return 0;
}

int translation_queue::find_dir_commit_parents_to_translate() {
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;

    if (source.find_dir_commit_parents_to_translate(cache, parent_alloc,
                                                    commits))
      return 1;
  }
  return 0;
}
