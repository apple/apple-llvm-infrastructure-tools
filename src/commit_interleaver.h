// commit_interleaver.h
#pragma once

#include "commit_source.h"
#include "error.h"
#include "git_cache.h"
#include "parsers.h"
#include "read_all.h"
#include "sha1_pool.h"
#include "split2monodb.h"
#include "translation_queue.h"
#include <array>

namespace {
struct progress_reporter {
  const long num_fparents_to_translate = 0;
  const long num_merges_to_translate = 0;
  const long num_side_to_translate = 0;
  long num_fparents_processed = 0;
  long num_merges_processed = 0;
  long num_side_processed = 0;

  static long count_fparents(const translation_queue &q) {
    long num_fparents = 0;
    for (auto &source : q.sources)
      num_fparents += source.num_fparents_to_translate;
    return num_fparents;
  }
  explicit progress_reporter(const translation_queue &q)
      : num_fparents_to_translate(count_fparents(q)),
        num_merges_to_translate(q.fparents.size() - num_fparents_to_translate),
        num_side_to_translate(q.commits.size() - num_fparents_to_translate) {}

  void report_side();
  void report_merge();
  void report_fparent();
  void report();
};

struct MergeTarget {
  const commit_source *source;
  sha1_ref base;
  sha1_ref mono;
  bool is_independent = false;
  explicit MergeTarget(const commit_source &source, sha1_ref base,
                       sha1_ref mono)
      : source(&source), base(base), mono(mono) {
    assert(source.is_repeat || base);
    assert(mono);
  }
};

struct MergeRequest {
  bool head_is_independent = false;
  bool is_octopus = false;

  std::vector<MergeTarget> &targets;
  std::vector<sha1_ref> &new_parents;
  std::vector<int> &parent_revs;
  std::vector<git_tree::item_type> &items;
  git_cache::commit_tree_buffers &buffers;

  explicit MergeRequest(std::vector<MergeTarget> &targets,
                        std::vector<sha1_ref> &new_parents,
                        std::vector<int> &parent_revs,
                        std::vector<git_tree::item_type> &items,
                        git_cache::commit_tree_buffers &buffers)
      : targets(targets), new_parents(new_parents), parent_revs(parent_revs),
        items(items), buffers(buffers) {
    targets.clear();
    new_parents.clear();
    parent_revs.clear();
    items.clear();
  }
};

struct commit_interleaver {
  constexpr static const size_t max_parents = 128;
  sha1_pool sha1s;
  git_cache cache;

  bool has_changed_any_heads = false;
  sha1_ref cmdline_start;
  sha1_ref head;
  sha1_ref repeated_head;
  commit_source *repeat = nullptr;
  std::vector<const char *> repeated_dir_names;
  dir_list dirs;
  translation_queue q;

  std::vector<char> stdin_bytes;

  commit_interleaver(split2monodb &db, mmapped_file &svn2git)
      : cache(db, svn2git, sha1s, dirs), q(cache, dirs) {
    dirs.list.reserve(64);
  }

  void set_source_head(commit_source &source, sha1_ref sha1) {
    q.set_source_head(source, sha1);
  }

  void set_initial_head(const textual_sha1 &sha1) {
    head = cmdline_start = sha1s.lookup(sha1);
  }

  int construct_tree(int head_p, commit_source &source, sha1_ref base_commit,
                     const std::vector<sha1_ref> &parents,
                     const std::vector<int> &parent_revs,
                     std::vector<git_tree::item_type> &items,
                     sha1_ref &tree_sha1);
  int make_partial_tree(const commit_source &source, sha1_ref base,
                        sha1_ref mono, std::vector<git_tree::item_type> &items,
                        dir_mask &source_dirs);
  int finish_making_tree_outside_source(int head_p, sha1_ref base_commit,
                                        dir_mask source_dirs,
                                        bool source_includes_root,
                                        const std::vector<sha1_ref> &parents,
                                        const std::vector<int> &parent_revs,
                                        std::vector<git_tree::item_type> &items,
                                        sha1_ref &tree_sha1);
  int index_parent_tree_items(int head_p, int p, dir_mask source_dirs,
                              bool source_includes_root, int &inactive_p,
                              sha1_ref parent, git_tree &tree,
                              std::array<int, dir_mask::max_size> &parent_for_d,
                              std::bitset<max_parents> &contributed,
                              const std::vector<int> &revs);
  int translate_parents(const commit_source &source, const commit_type &base,
                        std::vector<sha1_ref> &new_parents,
                        std::vector<int> &parent_revs, sha1_ref parent_override,
                        int override_p, int &new_srev);
  int fast_forward_initial_repeats(progress_reporter &progress);
  int translate_commit(commit_source &source, const commit_type &base,
                       std::vector<sha1_ref> &new_parents,
                       std::vector<int> &parent_revs,
                       std::vector<git_tree::item_type> &items,
                       git_cache::commit_tree_buffers &buffers,
                       sha1_ref *head = nullptr, int head_p = -1);
  void print_heads(FILE *file);

  void initialize_sources();
  int run();
  int run_impl();
  int prepare_sources();
  int merge_heads();
  int fast_forward();
  int interleave();
  int merge_goals();

  int merge_targets(MergeRequest &targets, sha1_ref &new_commit);
  int mark_independent_targets(MergeRequest &targets);
};
} // end namespace

void commit_interleaver::initialize_sources() {
  assert(q.sources.empty());
  int repeated_source_index = -1;
  if (dirs.repeated_dirs.any()) {
    q.sources.emplace_back(q.sources.size(), repeated_head);
    repeated_source_index = 0;
  }
  for (int d = 0, de = dirs.list.size(); d != de; ++d) {
    if (!dirs.tracked_dirs.test(d))
      continue;

    auto &dir = dirs.list[d];
    if (dirs.repeated_dirs.test(d)) {
      dir.source_index = repeated_source_index;
      q.sources[repeated_source_index].has_root |= dir.is_root;
      repeated_dir_names.push_back(dir.name);
      continue;
    }
    dir.source_index = q.sources.size();
    q.sources.emplace_back(q.sources.size(), dir, d);
  }
  if (repeated_source_index != -1)
    repeat = &q.sources[repeated_source_index];
}

int commit_interleaver::run() {
  assert(!q.sources.empty());

  // Initialize call_git, before launching threads.
  call_git_init();

  int status = run_impl();

  // Clean up worker threads.
  for (auto &source : q.sources)
    if (source.worker)
      source.worker->should_cancel = true;
  for (auto &source : q.sources)
    if (source.worker)
      if (source.worker->thread)
        source.worker->thread->join();

  if (!status) {
    print_heads(stdout);
    return 0;
  }

  fprintf(stderr, "interleave-progress: \n");
  print_heads(stderr);
  return status;
}

int commit_interleaver::run_impl() {
  // This is split out for better error reporting of interleave.
  if (prepare_sources() || merge_heads())
    return 1; // Has a good error already.
  if (fast_forward())
    return error("failed to fast-forward");
  if (interleave())
    return error("failed to interleave");
  // Has a good error already.
  return merge_goals();
}

int commit_interleaver::prepare_sources() {
  assert(!q.sources.empty());
  if (q.find_dir_commit_parents_to_translate() ||
      q.clean_initial_source_heads() || q.clean_initial_head(head) ||
      q.find_dir_commits(head) || q.interleave_dir_commits() ||
      q.ff_translated_dir_commits() ||
      q.find_repeat_commits_and_head(repeat, head) ||
      q.interleave_repeat_commits(repeat))
    return error("failed to process sources");

  return 0;
}

static void update_revs(int &new_srev, int &max_urev, int srev) {
  int urev = srev < 0 ? -srev : srev;
  if (urev > max_urev) {
    max_urev = urev;
    new_srev = -int(max_urev);
  }
}

int commit_interleaver::translate_parents(const commit_source &source,
                                          const commit_type &base,
                                          std::vector<sha1_ref> &new_parents,
                                          std::vector<int> &parent_revs,
                                          sha1_ref parent_override,
                                          int override_p,
                                          int &new_srev) {
  new_srev = 0;
  int max_urev = 0;
  auto process_future = [&](sha1_ref p) {
    auto *bc_index = source.worker->boundary_index_map.lookup(*p);
    if (!bc_index)
      return;

    auto &bc = source.worker->futures[bc_index->index];
    assert(bc.commit == p);
    if (bc.was_noted)
      return;

    cache.note_tree_raw(p, bc.rawtree);
    bc.was_noted = true;
  };
  auto add_parent = [&](sha1_ref p) {
    assert(!p->is_zeros());

    // Deal with this parent.
    if (base.has_boundary_parents)
      process_future(p);

    new_parents.push_back(p);
    int srev = 0;
    cache.compute_rev(p, /*is_split=*/!source.is_repeat, srev);
    parent_revs.push_back(srev);
    update_revs(new_srev, max_urev, srev);
  };

  // Wait for the worker to dig up information on boundary parents.
  if (base.has_boundary_parents)
    while (int(source.worker->last_ready_future) < base.last_boundary_parent)
      if (bool(source.worker->has_error))
        return 1;

  if (parent_override && override_p == -1) {
    assert(!base.num_parents);
    add_parent(parent_override);
    return 0;
  }

  for (int i = 0; i < base.num_parents; ++i) {
    assert(!base.parents[i]->is_zeros());

    // Override the first parent.
    if (i == override_p) {
      add_parent(parent_override);
      continue;
    }

    sha1_ref mono;
    if (cache.compute_mono(base.parents[i], mono))
      return error("parent " + base.parents[i]->to_string() + " of " +
                   base.commit->to_string() + " not translated");
    add_parent(mono);
  }
  return 0;
}

int commit_interleaver::make_partial_tree(
    const commit_source &source, sha1_ref base, sha1_ref mono,
    std::vector<git_tree::item_type> &items, dir_mask &source_dirs) {
  // Fill in source_dirs as we go.
  if (source.has_root || source.is_repeat) {
    if (source.is_repeat) {
      assert(mono);
      assert(!base);
      assert(source.dir_index == -1);
      source_dirs.bits |= dirs.repeated_dirs.bits;
    } else {
      assert(base);
      assert(source.dir_index != -1);
      assert(dirs.list[source.dir_index].is_root);
      source_dirs.set(source.dir_index);
    }
    git_tree tree;
    tree.sha1 = source.is_repeat ? mono : base;
    if (cache.ls_tree(tree))
      return 1;

    if (source.is_repeat) {
      for (int i = 0; i < tree.num_items; ++i) {
        assert(tree.items[i].sha1);
        if (int d = dirs.find_dir(tree.items[i].name))
          if (dirs.repeated_dirs.test(d))
            items.push_back(tree.items[i]);
      }
      return 0;
    }

    assert(base);
    items.reserve(items.size() + tree.num_items);
    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      if (dirs.is_dir(item.name))
        return error("root dir '-' conflicts with tracked dir '" +
                     base->to_string() + "'");
      items.push_back(item);
    }
    return 0;
  }

  assert(base);
  sha1_ref base_tree;
  if (cache.compute_commit_tree(base, base_tree))
    return error("failed to look up tree for '" + base->to_string() + "'");
  assert(base_tree);
  items.emplace_back();
  items.back().sha1 = base_tree;
  items.back().name = dirs.list[source.dir_index].name;
  items.back().type = git_tree::item_type::tree;
  source_dirs.set(source.dir_index);

  return 0;
}

int commit_interleaver::construct_tree(int head_p, commit_source &source,
                                       sha1_ref base_commit,
                                       const std::vector<sha1_ref> &parents,
                                       const std::vector<int> &revs,
                                       std::vector<git_tree::item_type> &items,
                                       sha1_ref &tree_sha1) {
  dir_mask source_dirs;
  return make_partial_tree(source, base_commit, sha1_ref(), items,
                           source_dirs) ||
         finish_making_tree_outside_source(
             head_p, base_commit, source_dirs,
             /*source_includes_root=*/source.has_root, parents, revs, items,
             tree_sha1);
}

int commit_interleaver::finish_making_tree_outside_source(
    int head_p, sha1_ref base_commit, dir_mask source_dirs,
    bool source_includes_root, const std::vector<sha1_ref> &parents,
    const std::vector<int> &revs, std::vector<git_tree::item_type> &items,
    sha1_ref &tree_sha1) {
  if (parents.size() > max_parents)
    return error(std::to_string(parents.size()) +
                 " is too many parents (max: " + std::to_string(max_parents) +
                 ")");

  if (head_p != -1)
    dirs.active_dirs.bits |= source_dirs.bits;

  // Pick parents for all the other directories.
  std::array<int, dir_mask::max_size> parent_for_d;
  parent_for_d.fill(-1);
  std::bitset<max_parents> contributed;
  std::array<git_tree, max_parents> trees;

  // Index the head first so that its content takes precedence.
  int inactive_p = -1;
  if (head_p != -1)
    if (index_parent_tree_items(
            head_p, head_p, source_dirs, source_includes_root, inactive_p,
            parents[head_p], trees[head_p], parent_for_d, contributed, revs))
      return 1;
  for (int p = 0, pe = parents.size(); p != pe; ++p)
    if (p != head_p)
      if (index_parent_tree_items(head_p, p, source_dirs, source_includes_root,
                                  inactive_p, parents[p], trees[p],
                                  parent_for_d, contributed, revs))
        return 1;

  auto get_dir_p = [&](int d) -> const int & {
    return dirs.active_dirs.test(d) ? parent_for_d[d] : inactive_p;
  };

  // Fill up the items for the tree.
  for (int p = 0, pe = parents.size(); p != pe; ++p) {
    if (!contributed.test(p))
      continue;

    const git_tree &tree = trees[p];
    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      if (source_includes_root && item.type != git_tree::item_type::tree)
        continue;

      int d = dirs.find_dir(item.name);
      assert(d != -1);
      if (!source_dirs.test(d))
        if (p == get_dir_p(d))
          items.push_back(item);
    }
  }

  // Sort and assert that we don't have any duplicates.
  std::sort(items.begin(), items.end(),
            [](const git_tree::item_type &lhs, const git_tree::item_type &rhs) {
              return strcmp(lhs.name, rhs.name) < 0;
            });
  assert(std::adjacent_find(items.begin(), items.end(),
                            [](const git_tree::item_type &lhs,
                               const git_tree::item_type &rhs) {
                              return strcmp(lhs.name, rhs.name) == 0;
                            }) == items.end());

  // Make the tree.
  if (items.size() > dir_mask::max_size)
    return error("too many items (max: " + std::to_string(dir_mask::max_size) +
                 "); constructing tree for " +
                 (base_commit ? base_commit.sha1->to_string()
                              : std::string("merge commit")));
  git_tree tree;
  tree.num_items = items.size();
  tree.items = cache.make_items(items.data(), items.data() + items.size());
  items.clear();
  if (cache.mktree(tree))
    return 1;
  tree_sha1 = tree.sha1;
  return 0;
}

int commit_interleaver::index_parent_tree_items(
    int head_p, int p, dir_mask source_dirs, bool source_includes_root,
    int &inactive_p, sha1_ref parent, git_tree &tree,
    std::array<int, dir_mask::max_size> &parent_for_d,
    std::bitset<max_parents> &contributed, const std::vector<int> &revs) {
  assert(!parent->is_zeros());
  tree.sha1 = parent;
  if (cache.ls_tree(tree))
    return 1;

  auto update_p = [&](int &dir_p, int p) {
    dir_p = p;
    contributed.set(p);
  };
  auto get_dir_p = [&](int d) -> int & {
    return dirs.active_dirs.test(d) ? parent_for_d[d] : inactive_p;
  };
  for (int i = 0; i < tree.num_items; ++i) {
    auto &item = tree.items[i];

    // Optimization: skip the directory lookup if this source is contributing
    // the monorepo root.
    if (source_includes_root && item.type != git_tree::item_type::tree)
      continue;

    int d = dirs.find_dir(item.name);
    if (d == -1)
      return error("no monorepo root to claim undeclared directory '" +
                    std::string(item.name) + "' in " +
                    parent->to_string());
    if (!dirs.list[d].is_root)
      if (item.type != git_tree::item_type::tree)
        return error("invalid non-tree for directory '" +
                      std::string(item.name) + "' in " +
                      parent->to_string());

    // The base commit takes priority even if we haven't seen it in a
    // first-parent commit yet.
    //
    // TODO: add a test where the base directory is possibly inactive,
    // because there are non-first-parent commits that get mapped ahead of
    // time.
    if (source_dirs.test(d))
      continue;

    int &dir_p = get_dir_p(d);

    // Check for a second object from the monorepo root.
    if (dir_p == p)
      continue;

    // Use the first parent found that has content for a directory.
    if (dir_p == -1) {
      update_p(dir_p, p);
      continue;
    }
    if (head_p == -1)
      assert(p > 0);
    else
      assert(p != head_p);

    // The first parent processed (which is the head, if any) wins for tracked
    // directories.
    //
    // TODO: add a testcase where a side-history commit (i.e., head_p is -1)
    // has a second parent with a higher rev than the first parent and
    // different content for a tracked directory.  Confirm the first parent's
    // version of the directory is used.
    //
    // FIXME: this logic is insufficient to make the following case sane:
    //
    //  - branch A is LLVM upstream
    //  - branch B tracks llvm and clang; is downstream of A
    //  - branch C tracks llvm (only)   ; is downstream of B
    //  - branch C sometimes merges directly from A
    //
    // since the clang in branch C will swing seemingly arbitrarily between a
    // version from A and a version from B, depending on the last merge.
    //
    // Instead, we'd want C to always pick the most recent B for its clang.
    // But we don't currently have a way to distinguish that.  Maybe there's
    // a way to annotate the LLVM svnbaserev with a branch depth, extending
    // the concept that a negative svnbaserev takes priority over a positive
    // one.
    if (dirs.active_dirs.test(d))
      continue;

    // Look up revs to pick a winner.
    //
    // Revs are stored signed, where negative indicates the parent itself is
    // not a commit from upstream LLVM (positive indicates that it is).
    const int old_srev = revs[dir_p];
    const int new_srev = revs[p];
    const int new_rev = new_srev < 0 ? -new_srev : new_srev;
    const int old_rev = old_srev < 0 ? -old_srev : old_srev;

    // Newer base SVN revision wins.
    if (old_rev > new_rev)
      continue;

    // If it's the same revision, prefer downstream content, then prior
    // parents.  Return early if we're not changing anything.
    if (old_rev == new_rev)
      if (old_srev <= 0 || new_srev >= 0)
        continue;

    // Change the parent.
    update_p(dir_p, p);
  }
  return 0;
}

void progress_reporter::report() {
  fprintf(stderr,
          "%8ld / %ld interleaved %8ld / %ld side %8ld / %ld generated\n",
          num_fparents_processed, num_fparents_to_translate, num_side_processed,
          num_side_to_translate, num_merges_processed, num_merges_to_translate);
}

void progress_reporter::report_side() {
  if (!(++num_side_processed % 50))
    report();
}

void progress_reporter::report_merge() {
  if (!(++num_merges_processed % 50))
    report();
}

void progress_reporter::report_fparent() {
  if (!(++num_fparents_processed % 50))
    report();
}

int commit_interleaver::fast_forward() {
  if (q.fparents.empty())
    return 0;

  // Try to fast-forward a bit.  Note that repeat commits have is_translated
  // set.
  auto fparent = q.fparents.back();
  int index = fparent.index;
  auto &source = q.sources[index];
  if (!fparent.is_translated)
    return 0;

  if (head) {
    sha1_ref ff_sha1;
    if (source.is_repeat)
      ff_sha1 = source.head;
    else if (commit_source::get_next_fparent_impl(fparent, cache, ff_sha1))
      return error("failed to get next fparent for fast-forward");
    if (head != ff_sha1)
      return 0;
  }

  // Fast-forward.
  do {
    fparent = q.fparents.back();
    sha1_ref mono;
    if (source.is_repeat)
      mono = fparent.commit;
    else if (cache.compute_mono(fparent.commit, mono))
      return error("expected '" + fparent.commit->to_string() +
                   "' to be translated already");

    set_source_head(source, fparent.commit);
    head = mono;

    q.fparents.pop_back();
  } while (!q.fparents.empty() && q.fparents.back().index == index &&
           q.fparents.back().is_translated);

  return 0;
}

int commit_interleaver::interleave() {
  // Persistent buffers.
  std::vector<MergeTarget> targets;
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  // Construct trees and commit them.
  progress_reporter progress(q);
  progress.report();
  while (!q.fparents.empty()) {
    auto fparent = q.fparents.back();
    q.fparents.pop_back();
    auto &source = q.sources[fparent.index];
    if (fparent.is_translated) {
      MergeRequest merge(targets, new_parents, parent_revs, items, buffers);
      sha1_ref base, mono;
      if (source.is_repeat) {
        mono = fparent.commit;
      } else {
        base = fparent.commit;
        if (cache.compute_mono(fparent.commit, mono))
          return error("expected '" + fparent.commit->to_string() +
                       "' to be translated already");
      }

      targets.emplace_back(source, base, mono);
      sha1_ref new_commit;
      if (merge_targets(merge, new_commit))
        return error("failed to generate merge of '" +
                     fparent.commit->to_string() + "'");
      set_source_head(source, fparent.commit);
      head = new_commit;
      progress.report_merge();
      continue;
    }

    assert(!source.is_repeat);
    if (!source.commits.count)
      return error("need to translate '" + fparent.commit->to_string() +
                   "' but out of commits");
    auto untranslated_first = q.commits.begin() + source.commits.first,
         untranslated_last = untranslated_first + source.commits.count;
    while (untranslated_first->commit != fparent.commit) {
      if (translate_commit(source, *untranslated_first, new_parents,
                           parent_revs, items, buffers))
        return error("failed to translate side commit '" +
                     untranslated_first->commit->to_string() + "'");

      if (++untranslated_first == untranslated_last)
        return error("first parent missing from side_commits");
      progress.report_side();
    }
    if (translate_commit(source, *untranslated_first, new_parents, parent_revs,
                         items, buffers, &head, fparent.head_p))
      return error("failed to translate commit '" +
                   untranslated_first->commit->to_string() + "'");
    set_source_head(source, fparent.commit);
    ++untranslated_first;
    source.commits.count = untranslated_last - untranslated_first;
    source.commits.first = untranslated_first - q.commits.begin();
    progress.report_fparent();
  }
  progress.report();

  return 0;
}

int commit_interleaver::merge_heads() {
  std::vector<MergeTarget> targets;
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  //fprintf(stderr, "merge heads\n");
  //fprintf(stderr, " - %s (head)\n",
  //        head ? head->to_string().c_str()
  //             : "0000000000000000000000000000000000000000");

  MergeRequest merge(targets, new_parents, parent_revs, items, buffers);
  merge.is_octopus = true;
  for (commit_source &source : q.sources) {
    //fprintf(stderr, " - %s (%s)\n",
    //        source.head ? source.head->to_string().c_str()
    //                    : "0000000000000000000000000000000000000000",
    //        source.is_repeat ? "repeat" : dirs.list[source.dir_index].name);
    if (!source.head)
      continue;

    // Look up the monorepo commit that corresponds to the split commit goal.
    sha1_ref monohead;
    if (source.is_repeat)
      monohead = source.head;
    else if (cache.compute_mono(source.head, monohead))
      return error("head " + source.head->to_string() + " not translated");

    merge.targets.emplace_back(
        source, source.is_repeat ? sha1_ref() : source.head, monohead);
  }

  if (merge.targets.empty())
    return 0;

  sha1_ref new_commit;
  if (merge_targets(merge, new_commit))
    return error("failed to merge heads");

  // Update all the head.
  head = new_commit;
  return 0;
}

int commit_interleaver::merge_goals() {
  std::vector<MergeTarget> targets;
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  MergeRequest merge(targets, new_parents, parent_revs, items, buffers);
  merge.is_octopus = true;
  for (commit_source &source : q.sources) {
    // Set up goal, head, and lambda to detect whether a directory is in
    // source.
    sha1_ref goal = source.goal;

    // Skip directories where there was no goal specified and where we reached
    // the goal while interleaving.
    if (!goal || goal == source.head)
      continue;

    // Look up the monorepo commit that corresponds to the split commit goal.
    sha1_ref monogoal;
    if (source.is_repeat)
      monogoal = goal;
    else if (cache.compute_mono(goal, monogoal))
      return error("goal " + goal->to_string() + " not translated");

    merge.targets.emplace_back(source, source.is_repeat ? sha1_ref() : goal,
                               monogoal);
  }

  if (targets.empty())
    return 0;

  sha1_ref new_commit;
  if (merge_targets(merge, new_commit))
    return error("failed to merge goals");

  // Update all the heads.
  head = new_commit;
  for (commit_source &source : q.sources)
    if (source.goal)
      set_source_head(source, source.goal);

  return 0;
}

int commit_interleaver::mark_independent_targets(MergeRequest &merge) {
  if (!merge.is_octopus) {
    assert(merge.targets.size() == 1);
    if (!head) {
      merge.targets.front().is_independent = true;
      return 0;
    }
    if (head == merge.targets.front().mono) {
      merge.head_is_independent = true;
      return 0;
    }

    // Just assume they're independent.  There's work done already when
    // preparing sources to ensure that generated merges will not be redundant,
    // only including them if they're newer than some mandatory commit to
    // merge.  It's too expensive to call git-merge-base on every repeat merge.
    merge.head_is_independent = merge.targets.front().is_independent = true;
    return 0;
  }

  // fprintf(stderr, "mark independent targets\n");
  assert(!merge.targets.empty());

  // Figure out which ones are independent.
  merge.head_is_independent = false;
  std::vector<sha1_ref> commits;
  if (head)
    commits.push_back(head);
  for (const MergeTarget &target : merge.targets)
    if (target.mono != head)
      commits.push_back(target.mono);
  if (commits.size() > 1)
    if (cache.merge_base_independent(commits))
      return error("failed to find independent target commits");

  // There should be at least one, or we have a logic bug.
  assert(!commits.empty());

  // Update the flags.
  std::sort(commits.begin(), commits.end());
  if (head)
    if (std::binary_search(commits.begin(), commits.end(), head))
      merge.head_is_independent = true;
  for (MergeTarget &target : merge.targets)
    if (target.mono != head)
      if (std::binary_search(commits.begin(), commits.end(), target.mono))
        target.is_independent = true;

  // Put the independent targets first.
  std::stable_sort(merge.targets.begin(), merge.targets.end(),
                   [](const MergeTarget &lhs, const MergeTarget &rhs) {
                     if (lhs.is_independent > rhs.is_independent)
                       return true;
                     if (lhs.is_independent < rhs.is_independent)
                       return false;

                     // Prefer putting a repeat commit first, since it makes
                     // the first commit from a new branch land better.
                     return lhs.source->is_repeat > rhs.source->is_repeat;
                   });

  // There should be at least one, or we have a logic bug.
  assert(merge.head_is_independent || merge.targets.front().is_independent);
  // fprintf(stderr, "sorted targets\n");
  // for (auto &target : merge.targets)
  //   fprintf(stderr, " - %s (independent = %d)\n",
  //           target.commit->to_string().c_str(), target.is_independent);
  return 0;
}

int commit_interleaver::merge_targets(MergeRequest &merge,
                                      sha1_ref &new_commit) {
  if (mark_independent_targets(merge))
    return 1;

  if (!merge.head_is_independent && !merge.is_octopus) {
    new_commit = merge.targets.front().mono;
    return 0;
  }

  // Function for adding parents.
  int new_srev = 0;
  int max_urev = 0;
  git_cache::parsed_metadata::string_ref max_cd;
  unsigned long long max_ct = 0;
  std::string first_subject;
  auto add_parent = [&](sha1_ref base, sha1_ref mono) {
    merge.new_parents.push_back(mono);
    int srev = 0;
    cache.compute_rev(mono, /*is_split=*/false, srev);
    merge.parent_revs.push_back(srev);
    update_revs(new_srev, max_urev, srev);

    if (!merge.is_octopus)
      return 0;

    // Extract the commit date.
    git_cache::parsed_metadata parsed;
    {
      const char *metadata;
      bool is_merge;
      sha1_ref first_parent;
      if (cache.compute_metadata(base ? base : mono, metadata, is_merge,
                                 first_parent))
        return error("failed to compute commit metadata for target '" +
                     mono->to_string() + "'");
      if (cache.parse_commit_metadata_impl(metadata, parsed))
        return error("failed to parse commit metadata for target '" +
                     mono->to_string() + "'");
    }
    if (head != mono && first_subject.empty())
      if (cache.extract_subject(first_subject, parsed.message))
        return error("failed to extract subject for target '" +
                     mono->to_string() + "'");

    // Choose the newest commit date.
    const char *current = parsed.cd.first;
    unsigned long long ct = 0;
    if (parse_num(current, ct) || *current != ' ')
      return error("failed to parse commit date timestamp for target '" +
                   mono->to_string() + "'");
    // fprintf(stderr, "   + ct = %llu\n", ct);
    if (ct > max_ct) {
      max_ct = ct;
      max_cd = parsed.cd;
    }
    return 0;
  };

  // Add the head as the initial parent.  If this is a new branch and there
  // were no commits to translate, then we may not have a head yet.  Also skip
  // it if somehow it's a descendent of one of the other commits.
  sha1_ref primary_parent;
  if (merge.head_is_independent) {
    primary_parent = head;
    if (add_parent(sha1_ref(), head))
      return 1;
  }

  // Add the target commits for each source where the target was not reached,
  // filling up the tree with items as appropriate.
  bool source_includes_root = false;
  dir_mask source_dirs;

  git_tree head_tree;

  std::vector<const char *> source_dir_names;
  for (const MergeTarget &target : merge.targets) {
    const commit_source &source = *target.source;
    if (make_partial_tree(source, target.base, target.mono, merge.items,
                          source_dirs))
      return error("failed to add items to merge");

    auto add_target_as_parent = [&]() {
      if (add_parent(target.base, target.mono))
        return 1;
      if (source.is_repeat)
        source_dir_names.insert(source_dir_names.end(),
                                repeated_dir_names.begin(),
                                repeated_dir_names.end());
      else
        source_dir_names.push_back(dirs.list[source.dir_index].name);

      // Keep track of whether the monorepo root is included.
      source_includes_root |= source.has_root;
      return 0;
    };

    if (target.is_independent) {
      if (add_target_as_parent())
        return 1;
      if (!primary_parent)
        head = primary_parent = target.mono;
      continue;
    } else if (!merge.is_octopus) {
      // We don't need to analyze this too much.
      add_target_as_parent();
      continue;
    }

    // Look up the head tree if we don't have it yet.
    if (!head_tree.sha1) {
      assert(primary_parent);
      head_tree.sha1 = primary_parent;
      if (cache.ls_tree(head_tree))
        return 1;
    }

    // This commit is in the ancestry of the head, but we still need to add it
    // if the content doesn't match up.  Compare content against the head.
    if (source.has_root || source.is_repeat) {
      git_tree tree;
      tree.sha1 = source.is_repeat ? target.mono : target.base;
      if (cache.ls_tree(tree))
        return 1;

      std::vector<git_tree::item_type> items;
      for (int i = 0; i < tree.num_items; ++i)
        if (int d = dirs.find_dir(tree.items[i].name))
          if (source.is_repeat ? dirs.repeated_dirs.test(d)
                               : dirs.list[d].is_root)
            items.push_back(tree.items[i]);
      for (int i = 0; i < head_tree.num_items; ++i)
        if (int d = dirs.find_dir(head_tree.items[i].name))
          if (source.is_repeat ? dirs.repeated_dirs.test(d)
                               : dirs.list[d].is_root)
            items.push_back(head_tree.items[i]);
      // Sort and see if we're matched up pairwise.
      std::sort(items.begin(), items.end());
      if (items.size() % 2) {
        add_target_as_parent();
        continue;
      }
      bool found_mismatch = false;
      for (int i = 0, ie = items.size(); i < ie; i += 2)
        if (items[i] < items[i + 1]) {
          found_mismatch = true;
          break;
        }
      if (found_mismatch)
        if (add_target_as_parent())
          return 1;
      continue;
    }

    // Just look up the one item in the head tree.
    sha1_ref base_tree;
    if (cache.compute_commit_tree(target.base, base_tree))
      return 1;
    assert(base_tree);
    bool found = false;
    for (int i = 0, ie = head_tree.num_items; i != ie; ++i) {
      auto &item = head_tree.items[i];
      if (item.sha1 != base_tree)
        continue;
      if (item.type != git_tree::item_type::tree)
        continue;
      if (strcmp(item.name, dirs.list[source.dir_index].name))
        continue;
      found = true;
      break;
    }
    if (!found)
      if (add_target_as_parent())
        return 1;
  }

  // Check whether we can skip the merge.
  assert(!merge.new_parents.empty());
  if (merge.new_parents.size() == 1) {
    new_commit = merge.new_parents.front();
    return 0;
  }

  // Add the other tree items.
  sha1_ref new_tree;
  if (finish_making_tree_outside_source(
          /*head_p=*/0, /*base_commit=*/sha1_ref(), source_dirs,
          source_includes_root, merge.new_parents, merge.parent_revs,
          merge.items, new_tree))
    return error("failed to make tree for targets merge");

  if (merge.is_octopus) {
    // Construct a commit message.
    git_cache::commit_tree_buffers &buffers = merge.buffers;
    buffers.message = "Merge ";
    cache.apply_dir_names_in_subject(buffers.message,
                                     dir_name_range(source_dir_names));
    if (merge.head_is_independent && merge.new_parents.size() == 2 &&
        !first_subject.empty()) {
      git_cache::commit_tree_buffers merged_buffers;
      buffers.message += ": ";
      buffers.message += first_subject;
    } else {
      buffers.message += '\n';
    }
    buffers.message += '\n';
    cache.apply_dir_name_trailers(buffers.message,
                                  dir_name_range(source_dir_names));
    cache.apply_metadata_env_names(buffers);
    cache.apply_merge_authorship(buffers, max_cd);
  } else {
    if (cache.parse_commit_metadata(
            merge.targets.front().base ? merge.targets.front().base
                                       : merge.targets.front().mono,
            merge.buffers, /*is_merge=*/true, dir_name_range(source_dir_names)))
      return 1;
  }

  return cache.commit_tree_impl(new_tree, merge.new_parents, new_commit,
                                merge.buffers) ||
         cache.set_base_rev(new_commit, new_srev);
}

void commit_interleaver::print_heads(FILE *file) {
  textual_sha1 sha1;
  auto set_sha1 = [&](sha1_ref ref) {
    if (ref)
      sha1 = textual_sha1(*ref);
    else
      memset(sha1.bytes, '0', 40);
  };
  set_sha1(head);
  fprintf(file, "%s", sha1.bytes);
  if (dirs.repeated_dirs.bits.any()) {
    set_sha1(repeated_head);
    fprintf(file, " %s:%s", sha1.bytes, "%");
  }
  for (int d = 0, de = dirs.list.size(); d != de; ++d) {
    if (!dirs.tracked_dirs.test(d))
      continue;
    if (dirs.repeated_dirs.test(d))
      continue;

    auto &dir = dirs.list[d];

    // "==" can fail if an error is returned while mapping the first commit.
    assert(bool(dir.head) <= dirs.active_dirs.test(d));
    set_sha1(dir.head);
    fprintf(file, " %s:%s", sha1.bytes, dir.name);
  }
  fprintf(file, "\n");
}

int commit_interleaver::translate_commit(
    commit_source &source, const commit_type &base,
    std::vector<sha1_ref> &new_parents, std::vector<int> &parent_revs,
    std::vector<git_tree::item_type> &items,
    git_cache::commit_tree_buffers &buffers, sha1_ref *head, int head_p) {
  assert(!head == (head_p == -1) || (head && !base.num_parents));
  assert(!source.is_repeat);
  new_parents.clear();
  parent_revs.clear();
  items.clear();
  dir_type &dir = q.dirs.list[source.dir_index];
  sha1_ref new_tree, new_commit;

  dir_name_range dir_names(dirs.list[source.dir_index].name);

  int rev = 0;
  if (translate_parents(source, base, new_parents, parent_revs,
                        /*parent_override=*/head ? *head : sha1_ref(), head_p,
                        rev) ||
      construct_tree(head_p, source, base.commit, new_parents, parent_revs,
                     items, new_tree) ||
      cache.commit_tree(base.commit, &dir, new_tree, new_parents, new_commit,
                        buffers, dir_names) ||
      cache.set_base_rev(new_commit, rev) ||
      cache.set_mono(base.commit, new_commit))
    return 1;

  if (!head)
    return 0;

  *head = new_commit;
  return 0;
}
