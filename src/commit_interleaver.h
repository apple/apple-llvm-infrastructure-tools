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

struct commit_interleaver {
  sha1_pool sha1s;
  git_cache cache;

  sha1_ref head;
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
                     const std::vector<sha1_ref> &parents,
                     const std::vector<int> &parent_revs,
                     std::vector<git_tree::item_type> &items,
                     sha1_ref &tree_sha1);
  int translate_parents(const commit_source &source, const commit_type &base,
                        std::vector<sha1_ref> &new_parents,
                        std::vector<int> &parent_revs, sha1_ref first_parent,
                        int &max_srev);
  int interleave();
  int interleave_impl();
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

  int d = -1;
  sources.emplace_back();
  commit_source &source = sources.back();
  if (parse_space(current) || parse_dir(current, d) || parse_newline(current))
    return 1;

  source.dir_index = d;
  source.is_root = !strcmp("-", dirs.list[d].name);
  dirs.list[d].source_index = sources.size() - 1;

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

  size_t num_fparents_before = fparents.size();
  int source_index = sources.size() - 1;
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

    long long last_ct = fparents.rbegin()[1].ct;
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

  sha1_trie<binary_sha1> skipped;
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
    if (is_boundary) {
      // Grab the metadata, which compute_mono might leverage if this is an
      // upstream git-svn commit.
      if (parse_through_null(current))
        return error("missing null charactor before boundary metadata");
      const char *metadata = current;
      if (parse_through_null(current))
        return error("missing null charactor after boundary metadata");
      cache.note_metadata(commit, metadata);
      if (parse_newline(current))
        return 1;

      if (!source.worker)
        source.worker.reset(new monocommit_worker);

      // Look up the monorepo commit.  Needs to be after noting the metadata to
      // avoid needing to shell out to git-log.
      sha1_ref mono;
      if (cache.compute_mono(commit, mono))
        return error("cannot find monorepo commit for boundary parent " +
                     commit->to_string());

      // Get the rev.
      int rev = 0;
      if (cache.lookup_rev(commit, rev) || !rev) {
        // Figure out the monorepo commit's rev, passing in the split commit's
        // metadata to suppress a new git-log call.  This is necessary for
        // checking the svnbase table (where split commits do not have
        // entries).
        if (cache.compute_rev_with_metadata(mono, rev, metadata))
          return error("cannot get rev for boundary parent " +
                       commit->to_string());
        (void)rev;
      } else {
        // Nice; compute_mono above filled this in.  Note it in the monorepo
        // commit as well.
        //
        // TODO: add a testcase where a second-level branch needs the
        // rev from a parent on a first-level branch.
        cache.note_rev(mono, rev);
      }

      // Mark it as a boundary commit and tell the worker about it.
      bool was_inserted = false;
      boundary_commit *bc =
          source.worker->boundary_index_map.insert(*mono, was_inserted);
      if (!bc)
        return error("failure to log a commit as a monorepo commit");
      bc->index = source.worker->futures.size();
      source.worker->futures.emplace_back();
      source.worker->futures.back().commit = mono;
      continue;
    }

    commits.emplace_back();
    commits.back().commit = commit;
    commits.back().tree = tree;
    while (!try_parse_space(current)) {
      // Check for a null character after the space, in case there are no
      // parents at all.
      //
      // TODO: add a testcase where there is a commit to map with no parents.
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
    cache.note_metadata(commit, metadata);

    if (parse_newline(current))
      return 1;

    // Now that we have metadata (necessary for an SVN revision, if relevant),
    // check if commit has already been translated.
    //
    // TODO: add a testcase where a split repository history has forked with
    // upstream LLVM and no splitref was added by mt-config.
    sha1_ref mono;
    if (!cache.compute_mono(commit, mono)) {
      assert(mono);
      commits.pop_back();
      parents.clear();
      bool was_inserted = false;
      skipped.insert(*commit, was_inserted);
      continue;
    }

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
                                          int &max_srev) {
  max_srev = 0;
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
    cache.compute_rev(p, srev);
    parent_revs.push_back(srev);
    int urev = srev < 0 ? -srev : srev;
    if (urev > max_urev) {
      max_urev = urev;
      max_srev = srev;
    }
  };

  // Wait for the worker to dig up information on boundary parents.
  if (base.has_boundary_parents)
    while (int(source.worker->last_ready_future) < base.last_boundary_parent)
      if (bool(source.worker->has_error))
        return 1;

  if (first_parent)
    add_parent(first_parent);
  for (int i = 0; i < base.num_parents; ++i) {
    // fprintf(stderr, "  - parent = %s\n",
    //         base.parents[i]->to_string().c_str());
    assert(!base.parents[i]->is_zeros());

    // Usually, override the first parent.  However, if this directory has not
    // yet been active on the branch then its original first parent may not be
    // in "first_parent"'s ancestory.
    if (first_parent && i == 0 && dirs.active_dirs.test(source.dir_index))
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

  // Add the base directory.
  const int base_d = source.dir_index;
  if (source.is_root) {
    git_tree tree;
    tree.sha1 = base_commit;
    if (cache.ls_tree(tree))
      return 1;
    for (int i = 0; i < tree.num_items; ++i) {
      if (tree.items[i].type == git_tree::item_type::tree)
        return error("root dir '-' has a sub-tree in " +
                     base_commit->to_string());
      items.push_back(tree.items[i]);
    }
  } else {
    sha1_ref base_tree;
    if (cache.compute_commit_tree(base_commit, base_tree))
      return 1;
    items.emplace_back();
    items.back().sha1 = base_tree;
    items.back().name = dirs.list[base_d].name;
    items.back().type = git_tree::item_type::tree;
  }
  if (is_head && !dirs.active_dirs.test(base_d))
    dirs.active_dirs.set(base_d);

  // Pick parents for all the other directories.
  auto find_d = [&](const char *name, bool &is_known_dir) {
    int d = dirs.lookup_dir(name, is_known_dir);
    if (is_known_dir)
      return d;

    // Anything unknown is part of the monorepo root.
    d = dirs.lookup_dir("-", is_known_dir);
    if (is_known_dir)
      return d;
    return -1;
  };
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

      bool is_known_dir = false;
      int d = find_d(item.name, is_known_dir);
      if (!is_known_dir)
        return error("no monorepo root to claim undeclared directory '" +
                     std::string(dirs.list[d].name) + "' in " +
                     parents[p]->to_string());
      if (!dirs.list[d].is_root)
        if (item.type != git_tree::item_type::tree)
          return error("invalid non-tree for directory '" +
                       std::string(dirs.list[d].name) + "' in " +
                       parents[p]->to_string());

      // The base commit takes priority even if we haven't seen it in a
      // first-parent commit yet.
      //
      // TODO: add a test where the base directory is possibly inactive,
      // because there are non-first-parent commits that get mapped ahead of
      // time.
      if (d == base_d)
        continue;

      int &dir_p = get_dir_p(d);

      // Use the first parent found that has content for a directory.
      if (dir_p == -1) {
        update_p(dir_p, p);
        continue;
      }

      // The first parent should get caught implicitly by the logic above.
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

      bool is_known_dir = false;
      int d = find_d(item.name, is_known_dir);
      assert(is_known_dir);
      assert(d != -1);
      if (d != base_d)
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

int commit_interleaver::interleave_impl() {
  // Construct trees and commit them.
  std::vector<sha1_ref> new_parents;
  std::vector<int> parent_revs;
  std::vector<git_tree::item_type> items;
  git_cache::commit_tree_buffers buffers;

  const long num_first_parents = q.fparents.size();
  long num_commits_processed = 0;
  auto report_progress = [&](long count = 0) {
    num_commits_processed += count;
    long num_first_parents_processed = num_first_parents - q.fparents.size();
    bool is_finished = count == 0;
    bool is_periodic = !(num_first_parents_processed % 50);
    if (is_finished == is_periodic)
      return 0;
    return fprintf(stderr,
                   "   %9ld / %ld first-parents mapped (%9ld / %ld commits)\n",
                   num_first_parents_processed, num_first_parents,
                   num_commits_processed, q.commits.size()) < 0
               ? 1
               : 0;
  };
  while (!q.fparents.empty()) {
    auto fparent = q.fparents.back();
    q.fparents.pop_back();
    auto &source = q.sources[fparent.index];
    auto &dir = dirs.list[source.dir_index];

    assert(source.commits.count);
    auto first = q.commits.begin() + source.commits.first,
         last = first + source.commits.count;
    auto original_first = first;
    while (first->commit != fparent.commit) {
      if (translate_commit(source, *first, new_parents, parent_revs, items,
                           buffers))
        return 1;

      if (++first == last)
        return error("first parent missing from all");
    }
    dir.head = first->commit;
    if (translate_commit(source, *first, new_parents, parent_revs, items,
                         buffers, &head))
      return 1;

    ++first;
    source.commits.count = last - first;
    source.commits.first = first - q.commits.begin();
    if (report_progress(first - original_first))
      return 1;
  }
  if (report_progress())
    return 1;

  if (head)
    print_heads(stdout);

  return 0;
}

void commit_interleaver::print_heads(FILE *file) {
  textual_sha1 sha1;
  if (head)
    sha1 = textual_sha1(*head);
  fprintf(file, "%s", sha1.bytes);
  for (int d = 0, de = dirs.list.size(); d != de; ++d) {
    if (!dirs.tracked_dirs.test(d))
      continue;

    auto &dir = dirs.list[d];
    assert(bool(dir.head) == dirs.active_dirs.test(d));
    if (dir.head)
      sha1 = textual_sha1(*dir.head);
    else
      memset(sha1.bytes, '0', 40);
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
  const char *dir = q.dirs.list[source.dir_index].name;
  sha1_ref new_tree, new_commit, first_parent_override;
  if (head)
    first_parent_override = *head;
  int rev = 0;
  // fprintf(stderr, "translate-commit = %s, tree = %s, num-parents = %d\n",
  //         base.commit->to_string().c_str(),
  //         base.tree->to_string().c_str(), base.num_parents);
  if (translate_parents(source, base, new_parents, parent_revs,
                        first_parent_override, rev) ||
      construct_tree(/*is_head=*/head, source, base.commit, new_parents,
                     parent_revs, items, new_tree) ||
      cache.commit_tree(base.commit, dir, new_tree, new_parents, new_commit,
                        buffers) ||
      cache.set_rev(new_commit, rev) || cache.set_mono(base.commit, new_commit))
    return 1;
  if (head)
    *head = new_commit;
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
