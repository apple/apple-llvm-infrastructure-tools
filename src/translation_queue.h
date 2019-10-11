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

  explicit translation_queue(git_cache &cache, dir_list &dirs)
      : cache(cache), pool(cache.pool), dirs(dirs) {}

  void set_source_head(commit_source &source, sha1_ref sha1);

  int interleave_dir_commits();
  int ff_translated_dir_commits();
  int clean_initial_head(sha1_ref &head);
  int clean_initial_source_heads();
  int find_dir_commits(sha1_ref head);
  int find_dir_commit_parents_to_translate();
  int find_repeat_commits_and_head(commit_source *repeat, sha1_ref &head);
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
  long long earliest_ct = LLONG_MAX;
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;

    long long earliest_ct_for_source = LLONG_MAX;
    if (source.find_dir_commits(cache, earliest_ct_for_source))
      return error("failed to find commits for '" +
                   std::string(dirs.list[source.dir_index].name) + "'");
    earliest_ct = std::min(earliest_ct, earliest_ct_for_source);
  }

  // Nothing to do if we have no untranslated commits.
  if (earliest_ct == LLONG_MAX)
    return 0;

  for (auto &source : sources) {
    if (source.is_repeat)
      continue;
    if (source.fparents.empty())
      continue;
    if (!source.head)
      continue;

    // Lock in existing already translated commits (to be merged onto this
    // branch) if there is a dir head, which means this tool already ran on
    // this branch.  We have a precise ancestry path so we should typically
    // use it, merging in commits that have been translated on other branches.
    //
    // However, if it would be possible to fast-forward the monorepo head into
    // this directory, do not lock anything in.  We don't want to start
    // generating merges until we actually have different content.
    //
    // Look at the first commit in the queue.  Note that it's not clear how
    // the main head could be missing here, but certainly you can
    // fast-forward from nothing.
    sha1_ref mono;
    if (!head || (!cache.compute_mono(source.fparents.back().commit, mono) &&
                  !cache.merge_base_is_ancestor(head, mono))) {
      // Locking these in means we'll generate merges from them, even though
      // they've been translated on *some* branch already.
      source.lock_in_start_dir_commits();
      earliest_ct = std::min(earliest_ct, source.fparents.back().ct);
    }
  }

  // For every other dir, look back further past commits to translate.  We want
  // a head that matches the commit date of the earliest other commit we're
  // handling.
  std::string since = "--since=" + std::to_string(earliest_ct);
  for (auto &source : sources) {
    if (source.is_repeat)
      continue;
    if (source.head)
      continue;
    if (source.find_dir_commits_to_match_and_update_head(cache, since))
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

int translation_queue::find_repeat_commits_and_head(commit_source *repeat,
                                                    sha1_ref &head) {
  if (!repeat)
    return 0;
  assert(repeat->is_repeat);
  if (repeat->goal == repeat->head)
    return repeat->skip_repeat_commits();

  // Set a lower bound for how far back to create merges.  Note that branches
  // can have a head from a start directive, without any of the sources having
  // an appropriate head.
  long long min_ct_to_merge = 0;
  if (head)
    if (cache.compute_ct(head, min_ct_to_merge))
      return error("failed to %ct of head '" + head->to_string() +
                   "' for stopping repeat '" + repeat->goal->to_string() + "'");

  // If it's possible to fast forward from head, figure out how far.
  bool can_ff_head = false;
  bool any_source_cts = false;
  if (!head || cache.merge_base_is_ancestor(head, repeat->goal)) {
    can_ff_head = true;
    for (auto &source : sources) {
      if (source.is_repeat)
        continue;

      if (!source.head)
        continue;

      // If there's a head and it's not an ancestor of the repeat commit goal,
      // then we should stretch repeat back to it.
      sha1_ref mono;
      if (cache.compute_mono(source.head, mono))
        return error("could not find monorepo hash for '" +
                     source.head->to_string() + "'");
      if (cache.merge_base_is_ancestor(mono, repeat->goal))
        continue;

      long long ct;
      if (cache.compute_ct(source.head, ct))
        return error("could not grab commit date of '" +
                      source.head->to_string() + "'");
      min_ct_to_merge = std::max(min_ct_to_merge, ct);
      any_source_cts = true;
    }
  }

  // Try harder to fast-forward.
  if (can_ff_head && !any_source_cts) {
    if (fparents.empty()) {
      // If there is nothing to translate and all the heads can fast-forward to
      // the repeat goal, then we only need to look back far enough to
      // determine the goal and refine the head.
      min_ct_to_merge = LLONG_MAX;
    } else {
      // Get the real ct, not the ct used for sorting.
      if (cache.compute_ct(fparents.back().commit, min_ct_to_merge))
        return error("could not grab commit date of '" +
                     fparents.back().commit->to_string() + "'");
    }
  }

  // We should have found this by now.
  assert(min_ct_to_merge);
  return repeat->find_repeat_commits_and_head(cache, min_ct_to_merge);
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
