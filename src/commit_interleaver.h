// commit_interleaver.h
#pragma once

#include "commit_source.h"
#include "error.h"
#include "git_cache.h"
#include "read_all.h"
#include "sha1_pool.h"
#include "split2monodb.h"
#include <array>

namespace {
struct translation_queue {
  bump_allocator parent_alloc;
  git_cache &cache;
  sha1_pool &pool;
  dir_list &dirs;
  std::vector<commit_source> sources;
  std::vector<fparent_type> fparents;
  std::vector<commit_type> commits;

  explicit translation_queue(git_cache &cache, dir_list &dirs)
      : cache(cache), pool(cache.pool), dirs(dirs) {}
  int parse_source(const char *&current, const char *end);
};

struct progress_reporter {
  const std::vector<fparent_type> &fparents;
  const std::vector<commit_type> &commits;
  const long num_first_parents;
  long num_commits_processed = 0;

  explicit progress_reporter(const std::vector<fparent_type> &fparents,
                             const std::vector<commit_type> &commits)
      : fparents(fparents), commits(commits),
        num_first_parents(fparents.size()) {}

  int report(long count = 0);
};

struct commit_interleaver {
  sha1_pool sha1s;
  git_cache cache;

  sha1_ref head;
  sha1_ref repeated_head;
  dir_list dirs;
  translation_queue q;

  std::vector<char> stdin_bytes;

  commit_interleaver(split2monodb &db, mmapped_file &svn2git)
      : cache(db, svn2git, sha1s, dirs), q(cache, dirs) {
    dirs.list.reserve(64);
  }

  int read_queue_from_stdin();
  void set_head(const textual_sha1 &sha1) { head = sha1s.lookup(sha1); }

  int construct_tree(bool is_head, commit_source &source, sha1_ref base_commit,
                     bool is_generated_merge,
                     const std::vector<sha1_ref> &parents,
                     const std::vector<int> &parent_revs,
                     std::vector<git_tree::item_type> &items,
                     sha1_ref &tree_sha1);
  int translate_parents(const commit_source &source, const commit_type &base,
                        std::vector<sha1_ref> &new_parents,
                        std::vector<int> &parent_revs, sha1_ref first_parent,
                        int &new_srev);
  int interleave();
  int interleave_impl();
  int fast_forward_initial_repeats(progress_reporter &progress);
  int translate_commit(commit_source &source, const commit_type &base,
                       std::vector<sha1_ref> &new_parents,
                       std::vector<int> &parent_revs,
                       std::vector<git_tree::item_type> &items,
                       git_cache::commit_tree_buffers &buffers,
                       sha1_ref *head = nullptr);
  void print_heads(FILE *file);
};
} // end namespace

int translation_queue::parse_source(const char *&current, const char *end) {
  auto parse_string = [](const char *&current, const char *s) {
    for (; *current && *s; ++current, ++s)
      if (*current != *s)
        return 1;
    return *s ? 1 : 0;
  };

  auto try_parse_string = [&parse_string](const char *&current, const char *s) {
    const char *temp = current;
    if (parse_string(temp, s))
      return 1;
    current = temp;
    return 0;
  };

  auto try_parse_space = [](const char *&current) {
    if (*current != ' ')
      return 1;
    ++current;
    return 0;
  };

  auto parse_space = [try_parse_space](const char *&current) {
    if (try_parse_space(current))
      return error("expected space");
    return 0;
  };

  auto parse_dir = [&](const char *&current, int &d) {
    for (const char *end = current; *end; ++end)
      if (*end == '\n') {
        bool found = false;
        if (current + 1 == end && *current == '%') {
          if (dirs.repeated_dirs.bits.none())
            return error("undeclared '%' in start directive");
          current = end;
          d = -1;
          return 0;
        }
        d = dirs.lookup_dir(current, end, found);
        if (!found)
          return error("undeclared directory '" + std::string(current, end) +
                       "' in start directive");
        if (!dirs.tracked_dirs.test(d))
          return error("untracked directory '" + std::string(current, end) +
                       "' in start directive");
        current = end;
        return 0;
      }
    return error("missing newline");
  };

  if (try_parse_string(current, "start"))
    return *current ? error("invalid start directive") : EOF;

  auto parse_newline = [](const char *&current) {
    if (*current != '\n')
      return error("expected newline");
    ++current;
    return 0;
  };

  auto parse_through_null = [end](const char *&current) {
    while (*current)
      ++current;
    if (current == end)
      return 1;
    ++current;
    return 0;
  };

  sources.emplace_back();
  commit_source &source = sources.back();
  {
    int d = -1;
    if (parse_space(current) || parse_dir(current, d) || parse_newline(current))
      return 1;

    if (d == -1) {
      source.is_repeat = true;
    } else {
      source.dir_index = d;
      source.is_root = !strcmp("-", dirs.list[d].name);
    }
  }

  auto parse_boundary = [](const char *&current, bool &is_boundary) {
    switch (*current) {
    default:
      return 1;
    case '-':
      is_boundary = true;
    case '>':
      break;
    }
    ++current;
    return 0;
  };

  auto parse_sha1 = [this](const char *&current, sha1_ref &sha1) {
    textual_sha1 text;
    if (text.from_input(current, &current))
      return error("invalid sha1");
    sha1 = pool.lookup(text);
    if (!sha1)
      return error("unexpected all-0 sha1 " + text.to_string());
    return 0;
  };

  auto parse_ct = [](const char *&current, long long &ct) {
    char *end = nullptr;
    ct = strtol(current, &end, 10);
    if (end == current || ct < 0)
      return error("invalid timestamp");
    current = end;
    return 0;
  };

  const size_t num_fparents_before = fparents.size();
  const int source_index = sources.size() - 1;
  while (true) {
    if (!try_parse_string(current, "all\n"))
      break;

    fparents.emplace_back();
    fparents.back().index = source_index;
    if (parse_sha1(current, fparents.back().commit) || parse_space(current) ||
        parse_ct(current, fparents.back().ct) || parse_newline(current))
      return 1;

    if (fparents.size() == 1 + num_fparents_before)
      continue;

    const long long last_ct = fparents.rbegin()[1].ct;
    if (fparents.back().ct <= last_ct)
      continue;

    // Fudge commit timestamp for future sorting purposes, ensuring that
    // fparents is monotonically non-increasing by commit timestamp within each
    // source.  We should never get here since (a) these are seconds since
    // epoch in UTC and (b) they get updated on rebase.  However, in theory a
    // committer could have significant clock skew.
    fprintf(stderr,
            "warning: apparent clock skew in %s\n"
            "   note: ancestor %s has earlier commit timestamp\n"
            "   note: using timestamp %lld instead of %lld for sorting\n",
            fparents.back().commit->to_string().c_str(),
            fparents.rbegin()[1].commit->to_string().c_str(), last_ct,
            fparents.back().ct);
    fparents.back().ct = last_ct;
  }

  sha1_trie<git_cache::sha1_single> skipped;
  source.commits.first = commits.size();
  std::vector<sha1_ref> parents;
  while (true) {
    if (!try_parse_string(current, "done\n"))
      break;

    // line ::= ( GT | MINUS ) commit SP tree ( SP parent )*
    bool is_boundary = false;
    sha1_ref commit, tree;
    if (parse_boundary(current, is_boundary) || parse_sha1(current, commit) ||
        parse_space(current) || parse_sha1(current, tree))
      return 1;

    // Warm the cache.
    cache.note_commit_tree(commit, tree);
    if (is_boundary || source.is_repeat) {
      bool is_merge = false;
      sha1_ref first_parent;

      // Get the first parent of the first repeat commit, to decide whether to
      // fast forward.
      if (!try_parse_space(current)) {
        if (*current) {
          if (parse_sha1(current, first_parent))
            return error("invalid first parent of boundary or repeat commit");

          // Check for a second parent, but don't parse it.
          if (*current)
            is_merge = true;
        }
      }

      if (!is_boundary)
        if (commits.size() == size_t(source.commits.first))
          source.first_repeat_first_parent = first_parent;

      // Grab the metadata, which compute_mono might leverage if this is an
      // upstream git-svn commit.
      if (parse_through_null(current))
        return error("missing null charactor before boundary metadata");
      const char *metadata = current;
      if (parse_through_null(current))
        return error("missing null charactor after boundary metadata");
      cache.note_metadata(commit, metadata, is_merge, first_parent);
      if (parse_newline(current))
        return 1;

      if (source.is_repeat) {
        if (is_boundary)
          continue;

        assert(source.is_repeat);
        commits.emplace_back();
        commits.back().commit = commit;
        commits.back().tree = tree;
        commits.back().is_generated_merge = true;
        continue;
      }

      if (!source.worker)
        source.worker.reset(new monocommit_worker);

      // Look up the monorepo commit.  Needs to be after noting the metadata to
      // avoid needing to shell out to git-log.
      sha1_ref mono;
      if (cache.compute_mono(commit, mono))
        return error("cannot find monorepo commit for boundary parent " +
                     commit->to_string());

      // Mark it as a boundary commit and tell the worker about it.
      bool was_inserted = false;
      boundary_commit *bc =
          source.worker->boundary_index_map.insert(*mono, was_inserted);
      if (!bc)
        return error("failure to log a commit as a monorepo commit");
      bc->index = source.worker->futures.size();
      source.worker->futures.emplace_back();
      source.worker->futures.back().commit = mono;

      // No need for rev-heroics if source.is_repeat, since then this is a
      // monorepo commit already.
      if (source.is_repeat)
        continue;

      // Get the rev.
      int rev = 0;
      if (cache.lookup_rev(commit, rev) || !rev) {
        // This can't be an upstream SVN commit or compute_mono would have
        // cached the rev.  Just check the svnbaserev table.
        if (cache.compute_base_rev(mono, rev))
          return error("cannot get rev for boundary parent " +
                       commit->to_string());
        (void)rev;
      } else {
        // compute_mono above filled this in.  Note it in the monorepo commit
        // as well.
        //
        // TODO: add a testcase where a second-level branch needs the
        // rev from a parent on a first-level branch.
        cache.note_rev(mono, rev);
      }
      continue;
    }

    assert(!source.is_repeat);
    commits.emplace_back();
    commits.back().commit = commit;
    commits.back().tree = tree;
    while (!try_parse_space(current)) {
      // Check for a null character after the space, in case there are no
      // parents at all.
      if (!*current) {
        if (parents.empty())
          break;
        return error("expected another parent after space");
      }

      parents.emplace_back();
      if (parse_sha1(current, parents.back()))
        return error("failed to parse parent");

      if (!source.worker)
        continue;

      sha1_ref mono;
      if (cache.lookup_mono(parents.back(), mono))
        continue;
      const boundary_commit *bc =
          source.worker->boundary_index_map.lookup(*mono);
      if (!bc)
        continue;

      // Mark how long to wait.
      commits.back().has_boundary_parents = true;
      if (bc->index > commits.back().last_boundary_parent)
        commits.back().last_boundary_parent = bc->index;
    }

    if (parse_through_null(current))
      return error("missing null charactor before metadata");
    const char *metadata = current;
    if (parse_through_null(current))
      return error("missing null charactor after metadata");
    cache.note_metadata(commit, metadata, /*is_merge=*/parents.size() > 1,
                        parents.empty() ? sha1_ref() : parents.front());

    if (parse_newline(current))
      return 1;

    // Now that we have metadata (necessary for an SVN revision, if relevant),
    // check if commit has already been translated.
    sha1_ref mono;
    if (!cache.compute_mono(commit, mono)) {
      assert(mono);
      commits.pop_back();
      parents.clear();
      bool was_inserted = false;
      skipped.insert(*commit, was_inserted);
      continue;
    }

    // We're committed to translating this commit.
    cache.note_being_translated(commit);
    commits.back().num_parents = parents.size();
    if (!parents.empty()) {
      commits.back().parents = new (parent_alloc) sha1_ref[parents.size()];
      std::copy(parents.begin(), parents.end(), commits.back().parents);
    }
    parents.clear();
  }

  // Clear out first parents we skipped over.
  if (!skipped.empty())
    fparents.erase(std::remove_if(fparents.begin(), fparents.end(),
                                  [&](const fparent_type &fparent) {
                                    return skipped.lookup(*fparent.commit) !=
                                           nullptr;
                                  }),
                   fparents.end());

  source.commits.count = commits.size() - source.commits.first;
  if (source.commits.count < fparents.size() - num_fparents_before)
    return error("first parents missing from commits");

  // Every commit should be a first parent commit.
  if (source.is_repeat)
    if (source.commits.count != fparents.size() - num_fparents_before)
      return error("unexpected non-first-parent repeat commits in stdin (" +
                   std::to_string(source.commits.count) + " commits; " +
                   std::to_string(fparents.size() - num_fparents_before) +
                   " fparents)");

  // Start looking up the tree data.
  if (source.worker)
    source.worker->start();
  return 0;
}

int commit_interleaver::translate_parents(const commit_source &source,
                                          const commit_type &base,
                                          std::vector<sha1_ref> &new_parents,
                                          std::vector<int> &parent_revs,
                                          sha1_ref first_parent,
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
    int urev = srev < 0 ? -srev : srev;
    if (urev > max_urev) {
      max_urev = urev;
      new_srev = -int(max_urev);
    }
  };

  // Wait for the worker to dig up information on boundary parents.
  if (base.has_boundary_parents)
    while (int(source.worker->last_ready_future) < base.last_boundary_parent)
      if (bool(source.worker->has_error))
        return 1;

  if (first_parent) {
    add_parent(first_parent);

    // Check for generated merges, which could be from a repeat or from the
    // first interleaved commit on a branch.
    if (base.is_generated_merge) {
      add_parent(base.commit);
      return 0;
    }
  }
  assert(!source.is_repeat);

  for (int i = 0; i < base.num_parents; ++i) {
    // fprintf(stderr, "  - parent = %s\n",
    //         base.parents[i]->to_string().c_str());
    assert(!base.parents[i]->is_zeros());

    // Override the first parent.
    if (first_parent && i == 0)
      continue;

    sha1_ref mono;
    if (cache.compute_mono(base.parents[i], mono))
      return error("parent " + base.parents[i]->to_string() + " of " +
                   base.commit->to_string() + " not translated");
    add_parent(mono);
  }
  return 0;
}

int commit_interleaver::construct_tree(bool is_head, commit_source &source,
                                       sha1_ref base_commit,
                                       bool is_generated_merge,
                                       const std::vector<sha1_ref> &parents,
                                       const std::vector<int> &revs,
                                       std::vector<git_tree::item_type> &items,
                                       sha1_ref &tree_sha1) {
  assert(!base_commit->is_zeros());

  std::array<int, dir_mask::max_size> parent_for_d;
  parent_for_d.fill(-1);

  constexpr static const size_t max_parents = 128;
  std::array<git_tree, max_parents> trees;
  std::bitset<max_parents> contributed;
  if (parents.size() > max_parents)
    return error(std::to_string(parents.size()) +
                 " is too many parents (max: " + std::to_string(max_parents) +
                 ")");

  // Create mask of directories handled by the base commit.
  dir_mask base_dirs;
  assert((source.dir_index == -1) == source.is_repeat);
  assert(!source.is_root || !source.is_repeat);
  assert(!source.is_repeat || is_generated_merge);
  if (is_generated_merge) {
    // Check that this is a first parent commit.
    if (!is_head)
      return error("unexpected non-first-parent repeat commit " +
                   base_commit->to_string());

    // Head should have been fast-forwarded to base_commit instead of calling
    // construct_tree if nothing has happened yet.  Similarly, if this is the
    // first commit for a directory, we only need a merge if another directory
    // beat us to the punch.
    assert(head);

    git_tree tree;
    tree.sha1 = base_commit;
    if (cache.ls_tree(tree))
      return 1;

    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      int d = dirs.find_dir(item.name);
      if (d == -1)
        return error("no monorepo root for path '" + std::string(item.name) +
                     "' in " + base_commit->to_string());

      if (source.is_repeat) {
        // Only pull in repeated directories up front.
        if (!dirs.repeated_dirs.test(d))
          continue;
      } else {
        // Only pull in the source directory up front.
        if (d != source.dir_index)
          continue;
      }

      base_dirs.set(d);
      items.push_back(item);
    }

    // Confirm commit has relevant dirs to repeat.
    if (base_dirs.bits.none())
      return error("base commit " + base_commit->to_string() +
                   " has no directories to merge");
  } else if (source.is_root) {
    base_dirs.set(source.dir_index);
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
    base_dirs.set(source.dir_index);
  }
  if (is_head)
    dirs.active_dirs.bits |= base_dirs.bits;

  // Pick parents for all the other directories.
  int inactive_p = -1;
  auto get_dir_p = [&](int d) -> int & {
    return dirs.active_dirs.test(d) ? parent_for_d[d] : inactive_p;
  };
  auto update_p = [&](int &dir_p, int p) {
    dir_p = p;
    contributed.set(p);
  };
  for (int p = 0, pe = parents.size(); p != pe; ++p) {
    assert(!parents[p]->is_zeros());
    git_tree &tree = trees[p];
    tree.sha1 = parents[p];
    if (cache.ls_tree(tree))
      return 1;

    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];

      // Optimization: skip the directory lookup if this source is contributing
      // the monorepo root.
      if (source.is_root && item.type != git_tree::item_type::tree)
        continue;

      int d = dirs.find_dir(item.name);
      if (d == -1)
        return error("no monorepo root to claim undeclared directory '" +
                     std::string(item.name) + "' in " +
                     parents[p]->to_string());
      if (!dirs.list[d].is_root)
        if (item.type != git_tree::item_type::tree)
          return error("invalid non-tree for directory '" +
                       std::string(item.name) + "' in " +
                       parents[p]->to_string());

      // The base commit takes priority even if we haven't seen it in a
      // first-parent commit yet.
      //
      // TODO: add a test where the base directory is possibly inactive,
      // because there are non-first-parent commits that get mapped ahead of
      // time.
      if (base_dirs.test(d))
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
      assert(p > 0);

      // First parent wins for tracked directories.
      //
      // TODO: add a testcase where a side-history commit (i.e., is_head is
      // false) has a second parent with a higher rev than the first parent and
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
  }

  // Fill up the items for the tree.
  for (int p = 0, pe = parents.size(); p != pe; ++p) {
    if (!contributed.test(p))
      continue;

    const git_tree &tree = trees[p];
    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      if (source.is_root && item.type != git_tree::item_type::tree)
        continue;

      int d = dirs.find_dir(item.name);
      assert(d != -1);
      if (!base_dirs.test(d))
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
                 "); constructing tree for " + base_commit.sha1->to_string());
  git_tree tree;
  tree.num_items = items.size();
  tree.items = cache.make_items(items.data(), items.data() + items.size());
  items.clear();
  if (cache.mktree(tree))
    return 1;
  tree_sha1 = tree.sha1;
  return 0;
}

int commit_interleaver::interleave() {
  int status = interleave_impl();

  // Clean up worker threads.
  for (auto &source : q.sources)
    if (source.worker)
      source.worker->should_cancel = true;
  for (auto &source : q.sources)
    if (source.worker)
      if (source.worker->thread)
        source.worker->thread->join();

  if (!status)
    return 0;

  fprintf(stderr, "interleave-progress: \n");
  print_heads(stderr);
  return status;
}

int progress_reporter::report(long count) {
  num_commits_processed += count;
  long num_first_parents_processed = num_first_parents - fparents.size();
  bool is_finished = count == 0;
  bool is_periodic = !(num_first_parents_processed % 50);
  if (is_finished == is_periodic)
    return 0;
  return fprintf(stderr,
                 "   %9ld / %ld first-parents mapped (%9ld / %ld commits)\n",
                 num_first_parents_processed, num_first_parents,
                 num_commits_processed, commits.size()) < 0
             ? 1
             : 0;
}

int commit_interleaver::fast_forward_initial_repeats(
    progress_reporter &progress) {
  auto repeats_index = q.fparents.back().index;
  auto &repeats = q.sources[repeats_index];
  assert(repeats.commits.count != 0);

  // If this branch has started, double-check that this will actually be a fast
  // forward.
  if (head)
    if (head != repeats.first_repeat_first_parent)
      return 0;

  // Don't generate merge commits if we can just fast-forward.
  auto first = q.commits.begin() + repeats.commits.first,
       last = first + repeats.commits.count;
  while (!q.fparents.empty()) {
    const auto &fparent = q.fparents.back();
    if (fparent.index != repeats_index)
      break;

    assert(first->commit == fparent.commit);
    head = fparent.commit;
    q.fparents.pop_back();
    progress.report(1);
    ++first;
  }
  assert(head);
  repeats.commits.count = last - first;
  repeats.commits.first = first - q.commits.begin();
  repeated_head = head;
  return 0;
}

int commit_interleaver::interleave_impl() {
  // Construct trees and commit them.
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  progress_reporter progress(q.fparents, q.commits);
  if (!q.fparents.empty() && q.sources[q.fparents.back().index].is_repeat)
    if (fast_forward_initial_repeats(progress))
      return 1;
  while (!q.fparents.empty()) {
    auto fparent = q.fparents.back();
    q.fparents.pop_back();
    auto &source = q.sources[fparent.index];
    assert(source.commits.count);
    auto first = q.commits.begin() + source.commits.first,
         last = first + source.commits.count;
    const auto original_first = first;
    while (first->commit != fparent.commit) {
      if (translate_commit(source, *first, new_parents, parent_revs, items,
                           buffers))
        return 1;

      if (++first == last)
        return error("first parent missing from all");
    }
    if (translate_commit(source, *first, new_parents, parent_revs, items,
                         buffers, &head))
      return 1;

    ++first;
    source.commits.count = last - first;
    source.commits.first = first - q.commits.begin();
    if (progress.report(first - original_first))
      return 1;
  }
  if (progress.report())
    return 1;

  if (head)
    print_heads(stdout);

  return 0;
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
    git_cache::commit_tree_buffers &buffers, sha1_ref *head) {
  new_parents.clear();
  parent_revs.clear();
  items.clear();
  dir_type *dir = source.is_repeat ? nullptr : &q.dirs.list[source.dir_index];
  sha1_ref new_tree, new_commit, first_parent_override;

  // Don't override parents for the first interleaved commit from a directory.
  // We don't want to change the number of parents of the commit (add the
  // head), and the translation of its first parent is probably not going to be
  // in the ancestry.  We'll fix up head below, after translation.
  bool should_override_first_parent = [&]() {
    if (!head)
      return false;
    if (!*head)
      return false;
    if (!dir)
      return true;
    if (dir->head)
      return true;

    // The first parent could be in the ancestry after all.  This happens at
    // most once per generated branch per split repo, so we can afford to check
    // and prune those edges.
    if (!base.num_parents)
      return true;

    // TODO: --is-ancestor would be faster than a full merge-base.
    sha1_ref split_fparent = base.parents[0], mono_fparent, merge_base;
    if (!cache.lookup_mono(split_fparent, mono_fparent))
      if (!cache.merge_base(mono_fparent, *head, merge_base))
        if (merge_base == mono_fparent)
          return true;
    return false;
  }();

  // Okay, override it.
  if (should_override_first_parent)
    first_parent_override = *head;

  int rev = 0;
  // fprintf(stderr, "translate-commit = %s, tree = %s, num-parents = %d\n",
  //         base.commit->to_string().c_str(),
  //         base.tree->to_string().c_str(), base.num_parents);
  if (translate_parents(source, base, new_parents, parent_revs,
                        first_parent_override, rev) ||
      construct_tree(/*is_head=*/head, source, base.commit,
                     /*is_generated_merge=*/base.is_generated_merge,
                     new_parents, parent_revs, items, new_tree) ||
      cache.commit_tree(base.commit, dir, new_tree, new_parents, new_commit,
                        buffers) ||
      cache.set_base_rev(new_commit, rev))
    return 1;
  if (!head)
    return cache.set_mono(base.commit, new_commit);

  if (!should_override_first_parent && *head) {
    // Prepare to generate a merge to interleave the first commit.
    new_parents.clear();
    parent_revs.clear();
    items.clear();
    rev = 0;
    commit_type translated;
    translated.commit = new_commit;
    translated.tree = new_tree;
    translated.has_boundary_parents = false;
    translated.is_generated_merge = true;

    // Merge the just-translated commit into head.
    new_tree = new_commit = sha1_ref();
    if (translate_parents(source, translated, new_parents, parent_revs,
                          *head, rev) ||
        construct_tree(/*is_head=*/true, source, translated.commit,
                       /*is_generated_merge*/true, new_parents, parent_revs,
                       items, new_tree) ||
        cache.commit_tree(base.commit, /*dir=*/nullptr, new_tree, new_parents,
                          new_commit, buffers) ||
        cache.set_base_rev(new_commit, rev))
      return 1;
  }

  if (!source.is_repeat && cache.set_mono(base.commit, new_commit))
    return 1;

  *head = new_commit;
  if (source.is_repeat)
    repeated_head = base.commit;
  else
    dir->head = base.commit;
  return 0;
}

int commit_interleaver::read_queue_from_stdin() {
  // We will interleave first parent commits, sorting by commit timestamp,
  // putting the earliest at the back of the vector and top of the stack.  Use
  // stable sort to prevent reordering within a source.
  auto by_non_increasing_commit_timestamp = [](const fparent_type &lhs,
                                               const fparent_type &rhs) {
    // Within a source, rely entirely on initial topological
    // sort.
    if (lhs.index == rhs.index)
      return false;
    return lhs.ct > rhs.ct;
  };

  if (read_all(/*fd=*/0, stdin_bytes))
    return 1;

  // Initialize call_git, before launching threads.
  call_git_init();

  stdin_bytes.push_back(0);
  const char *current = stdin_bytes.data();
  const char *end = stdin_bytes.data() + stdin_bytes.size() - 1;
  {
    int status = 0;
    while (!status) {
      size_t orig_num_fparents = q.fparents.size();
      status = q.parse_source(current, end);

      // We can assert here since parse_source is supposed to fudge any
      // inconsistencies so that sorting later is legal.
      assert(std::is_sorted(q.fparents.begin() + orig_num_fparents,
                            q.fparents.end(),
                            by_non_increasing_commit_timestamp));
    }
    if (status != EOF)
      return 1;
  }
  if (q.sources.empty())
    return 0;

  // Interleave first parents.
  std::stable_sort(q.fparents.begin(), q.fparents.end(),
                   by_non_increasing_commit_timestamp);
  return 0;
}
