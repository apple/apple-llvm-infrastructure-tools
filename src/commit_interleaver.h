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
  sha1_ref commit;
  bool is_independent = false;
  explicit MergeTarget(const commit_source &source, sha1_ref commit)
      : source(&source), commit(commit) {
    assert(commit);
  }
};

struct MergeRequest {
  bool assume_independent;
  bool head_is_independent;
  std::vector<MergeTarget> targets;

  MergeRequest() { init(); }
  void init() {
    assume_independent = false;
    head_is_independent = false;
    targets.clear();
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

  int merge_targets(MergeRequest &targets, sha1_ref &new_commit,
                    std::vector<sha1_ref> &new_parents,
                    std::vector<int> &parent_revs);
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
      q.ff_translated_dir_commits() || q.find_repeat_commits(repeat) ||
      q.find_repeat_head(repeat) || q.interleave_repeat_commits(repeat))
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

int commit_interleaver::construct_tree(int head_p, commit_source &source,
                                       sha1_ref base_commit,
                                       const std::vector<sha1_ref> &parents,
                                       const std::vector<int> &revs,
                                       std::vector<git_tree::item_type> &items,
                                       sha1_ref &tree_sha1) {
  assert(!source.is_repeat);
  assert(!base_commit->is_zeros());
  assert(source.dir_index != -1);

  // Create mask of directories handled by the base commit.
  dir_mask source_dirs;
  if (source.has_root) {
    assert(source.dir_index != -1);
    assert(dirs.list[source.dir_index].is_root);
    source_dirs.set(source.dir_index);
    git_tree tree;
    tree.sha1 = base_commit;
    if (cache.ls_tree(tree))
      return 1;

    for (int i = 0; i < tree.num_items; ++i)
      if (dirs.is_dir(tree.items[i].name))
        return error("root dir '-' conflicts with tracked dir '" +
                     base_commit->to_string() + "'");
    items.resize(tree.num_items);
    std::copy(tree.items, tree.items + tree.num_items, items.begin());
  } else {
    sha1_ref base_tree;
    if (cache.compute_commit_tree(base_commit, base_tree))
      return 1;
    items.emplace_back();
    items.back().sha1 = base_tree;
    items.back().name = dirs.list[source.dir_index].name;
    items.back().type = git_tree::item_type::tree;
    source_dirs.set(source.dir_index);
  }

  return finish_making_tree_outside_source(
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

  // Try to fast-forward a bit.
  auto fparent = q.fparents.back();
  int index = fparent.index;
  auto &source = q.sources[index];
  if (!fparent.is_translated && !source.is_repeat)
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
    sha1_ref mono;
    if (source.is_repeat)
      mono = fparent.commit;
    else if (cache.compute_mono(fparent.commit, mono))
      return error("expected '" + fparent.commit->to_string() +
                   "' to be translated already");

    set_source_head(source, fparent.commit);
    head = mono;

    q.fparents.pop_back();
    fparent = q.fparents.back();
  } while (!q.fparents.empty() && q.fparents.back().index == index &&
           (q.fparents.back().is_translated || source.is_repeat));

  return 0;
}

int commit_interleaver::interleave() {
  // Construct trees and commit them.
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  progress_reporter progress(q);
  progress.report();
  while (!q.fparents.empty()) {
    auto fparent = q.fparents.back();
    q.fparents.pop_back();
    auto &source = q.sources[fparent.index];
    if (fparent.is_translated || source.is_repeat) {
      MergeRequest merge;
      // FIXME: this is probably an important optimization, but right now the
      // assumption is wrong.
      //
      // merge.assume_independent = true;
      sha1_ref mono;
      if (source.is_repeat)
        mono = fparent.commit;
      else if (cache.compute_mono(fparent.commit, mono))
        return error("expected '" + fparent.commit->to_string() +
                     "' to be translated already");

      merge.targets.emplace_back(source, mono);
      sha1_ref new_commit;
      if (merge_targets(merge, new_commit, new_parents, parent_revs))
        return error("failed to generate merge of '" +
                     fparent.commit->to_string() + "'");
      set_source_head(source, fparent.commit);
      head = new_commit;
      progress.report_merge();
      continue;
    }

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
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;

  //fprintf(stderr, "merge heads\n");
  //fprintf(stderr, " - %s (head)\n",
  //        head ? head->to_string().c_str()
  //             : "0000000000000000000000000000000000000000");

  MergeRequest merge;
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

    merge.targets.push_back(MergeTarget(source, monohead));
  }

  if (merge.targets.empty())
    return 0;

  sha1_ref new_commit;
  if (merge_targets(merge, new_commit, new_parents, parent_revs))
    return error("failed to merge heads");

  // Update all the head.
  head = new_commit;
  return 0;
}

int commit_interleaver::merge_goals() {
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;

  MergeRequest merge;
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

    merge.targets.push_back(MergeTarget(source, monogoal));
  }

  if (merge.targets.empty())
    return 0;

  sha1_ref new_commit;
  if (merge_targets(merge, new_commit, new_parents, parent_revs))
    return error("failed to merge goals");

  // Update all the heads.
  head = new_commit;
  for (commit_source &source : q.sources)
    if (source.goal)
      set_source_head(source, source.goal);

  return 0;
}

int commit_interleaver::mark_independent_targets(MergeRequest &merge) {
  // fprintf(stderr, "mark independent targets\n");
  assert(!merge.targets.empty());
  if (merge.assume_independent) {
    // fprintf(stderr, " - assume all\n");
    if (head)
      merge.head_is_independent = true;
    for (auto &t : merge.targets)
      if (t.commit != head)
        t.is_independent = true;
    return 0;
  }

  // Figure out which ones are independent.
  merge.head_is_independent = false;
  std::vector<sha1_ref> commits;
  if (head)
    commits.push_back(head);
  for (const MergeTarget &target : merge.targets)
    if (target.commit != head)
      commits.push_back(target.commit);
  if (commits.size() > 1)
    if (cache.merge_base_independent(commits))
      return error("failed to find independent target commits");

  // There should be at least one, or we have a logic bug.
  assert(!commits.empty());

  // Update the flags.
  std::sort(commits.begin(), commits.end());
  if (head)
    if (std::binary_search(commits.begin(), commits.end(), head)) {
      // fprintf(stderr, " - %s independent (head)\n", head->to_string().c_str());
      merge.head_is_independent = true;
    }
  for (MergeTarget &target : merge.targets)
    if (target.commit != head)
      if (std::binary_search(commits.begin(), commits.end(), target.commit)) {
        //fprintf(stderr, " - %s independent (%s)\n",
        //        target.commit->to_string().c_str(),
        //        target.source->is_repeat
        //            ? "repeat"
        //            : dirs.list[target.source->dir_index].name);
        target.is_independent = true;
      }

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

int commit_interleaver::merge_targets(MergeRequest &merge, sha1_ref &new_commit,
                                      std::vector<sha1_ref> &new_parents,
                                      std::vector<int> &parent_revs) {
  if (mark_independent_targets(merge))
    return 1;

  // Function for adding parents.
  int new_srev = 0;
  int max_urev = 0;
  git_cache::parsed_metadata::string_ref max_cd;
  unsigned long long max_ct = 0;
  std::string first_subject;
  new_parents.clear();
  parent_revs.clear();
  auto add_parent = [&](sha1_ref mono_p) {
    // fprintf(stderr, " - add parent: %s\n", mono_p->to_string().c_str());
    new_parents.push_back(mono_p);
    int srev = 0;
    cache.compute_rev(mono_p, /*is_split=*/false, srev);
    parent_revs.push_back(srev);
    update_revs(new_srev, max_urev, srev);

    // Extract the commit date.
    git_cache::parsed_metadata parsed;
    {
      const char *metadata;
      bool is_merge;
      sha1_ref first_parent;
      if (cache.compute_metadata(mono_p, metadata, is_merge, first_parent))
        return error("failed to compute commit metadata for target '" +
                     mono_p->to_string() + "'");
      fprintf(stderr, "monop=%s\nmetadata = %s\n", mono_p->to_string().c_str(),
              metadata);
      if (cache.parse_commit_metadata_impl(metadata, parsed))
        return error("failed to parse commit metadata for target '" +
                     mono_p->to_string() + "'");
    }
    if (head != mono_p && first_subject.empty())
      if (cache.extract_subject(first_subject, parsed.message))
        return error("failed to extract subject for target '" +
                     mono_p->to_string() + "'");

    // Choose the newest commit date.
    const char *current = parsed.cd.first;
    unsigned long long ct = 0;
    if (parse_num(current, ct) || *current != ' ')
      return error("failed to parse commit date timestamp for target '" +
                   mono_p->to_string() + "'");
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
  if (merge.head_is_independent)
    if (add_parent(head))
      return 1;

  // Track whether we need to replace the head.
  bool needs_a_head = false;
  git_tree head_tree;
  if (merge.head_is_independent) {
    head_tree.sha1 = head;
    if (cache.ls_tree(head_tree))
      return error("head " + head->to_string() + " has no tree");
  } else {
    needs_a_head = true;
  }
  // fprintf(stderr, " - needs_a_head = %d\n", needs_a_head);

  // Add the target commits for each source where the target was not reached,
  // filling up the tree with items as appropriate.
  bool source_includes_root = false;
  std::vector<git_tree::item_type> items;
  dir_mask source_dirs;

  std::vector<const char *> source_dir_names;
  for (const MergeTarget &target : merge.targets) {
    const commit_source &source = *target.source;

    // Skip this if the first parent already has the content.
    if (!new_parents.empty())
      if (target.commit == new_parents.front())
        continue;

    // Look up the tree for the target.
    git_tree goal_tree;
    goal_tree.sha1 = target.commit;
    if (cache.ls_tree(goal_tree))
      return error("target " + target.commit->to_string() + " has no tree");

    if (needs_a_head) {
      needs_a_head = false;
      assert(target.is_independent);
      head = target.commit;
      head_tree = goal_tree;
    }

    // Add tree items owned by this dir.  There will likely be multiple items
    // for the monorepo root.
    std::vector<git_tree::item_type> candidate_items;
    int num_root_items = 0;
    for (int i = 0, ie = goal_tree.num_items; i != ie; ++i) {
      auto &item = goal_tree.items[i];
      int d = dirs.find_dir(item.name);
      if (d == -1)
        return error("no monorepo root for path '" + std::string(item.name) +
                     "' in target '" + target.commit->to_string() + "'");

      if (d != source.dir_index)
        if (!source.is_repeat || !dirs.repeated_dirs.test(d))
          continue;

      assert(dirs.list[d].is_root || !source_dirs.test(d));
      source_dirs.set(d);
      if (dirs.list[d].is_root)
        ++num_root_items;
      if (target.is_independent)
        items.push_back(item);
      else
        candidate_items.push_back(item);
    }

    auto add_target_as_parent = [&]() {
      if (add_parent(target.commit))
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
      continue;
    }

    // This commit is in the ancestry of the head, but we still need to add it
    // if the content doesn't match up.
    assert(head);
    assert(target.commit != head);
    bool found_mismatch = false;
    for (auto &cand_item : candidate_items) {
      auto last_head_item = head_tree.items + head_tree.num_items;
      auto *head_item = std::find_if(head_tree.items, last_head_item,
                                     [&](const git_tree::item_type &head_item) {
                                       return !strcmp(head_item.name, cand_item.name);
                                     });
      if (head_item == last_head_item || cand_item.sha1 != head_item->sha1 ||
          cand_item.type != head_item->type) {
        found_mismatch = true;
        break;
      }
    }

    // Check number of root items matches.
    if (!found_mismatch && source.has_root) {
      for (int i = 0; i < head_tree.num_items; ++i) {
        int d = dirs.find_dir(head_tree.items[i].name);
        assert(d != -1);
        if (dirs.list[d].is_root)
          --num_root_items;
      }
      found_mismatch |= num_root_items != 0;
    }

    // We can skip this target if there's no mismatch.
    if (!found_mismatch)
      continue;

    // Okay, we need this one.
    items.insert(items.end(), candidate_items.begin(), candidate_items.end());
    if (add_target_as_parent())
      return 1;
  }

  assert(!new_parents.empty());
  if (new_parents.size() == 1) {
    // Check whether there is a merge.
    assert(!head || new_parents.front() == head);
    new_commit = head;
    return 0;
  }

  // Add the other tree items.
  sha1_ref new_tree;
  if (finish_making_tree_outside_source(
          /*head_p=*/0, /*base_commit=*/sha1_ref(), source_dirs,
          source_includes_root, new_parents, parent_revs, items, new_tree))
    return error("failed to make tree for targets merge");

  // Construct a commit message.
  git_cache::commit_tree_buffers buffers;
  buffers.message = "Merge ";
  cache.apply_dir_names_in_subject(buffers.message,
                                   dir_name_range(source_dir_names));
  if (merge.head_is_independent && new_parents.size() == 2 &&
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
  return cache.commit_tree_impl(new_tree, new_parents, new_commit, buffers) ||
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
