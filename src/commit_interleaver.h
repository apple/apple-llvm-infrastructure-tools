// commit_interleaver.h
#pragma once

#include "commit_source.h"
#include "error.h"
#include "git_cache.h"
#include "read_all.h"
#include "sha1_pool.h"
#include "split2monodb.h"

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
  bool has_root = false;
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
                        int &max_rev);
  int interleave();
  int interleave_impl();
  int translate_commit(commit_source &source, const commit_type &base,
                       std::vector<sha1_ref> &new_parents,
                       std::vector<int> &parent_revs,
                       std::vector<git_tree::item_type> &items,
                       git_cache::commit_tree_buffers &buffers,
                       sha1_ref *head = nullptr);
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
      // Grab the metadata, which get_mono might leverage if this is an
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
      if (cache.get_mono(commit, mono))
        return error("cannot find monorepo commit for boundary parent " +
                     commit->to_string());

      // Get the rev.
      int rev = 0;
      if (cache.lookup_rev(commit, rev) && rev) {
        // Figure out the monorepo commit's rev, passing in the split commit's
        // metadata to suppress a new git-log call.  This is necessary for
        // checking the svnbase table (where split commits do not have
        // entries).
        if (cache.get_rev_with_metadata(mono, rev, metadata))
          return error("cannot get rev for boundary parent " +
                       commit->to_string());
        (void)rev;
      } else {
        // Nice; get_mono above filled this in.  Note it in the monorepo commit
        // as well.
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

    commits.back().num_parents = parents.size();
    if (!parents.empty()) {
      commits.back().parents = new (parent_alloc) sha1_ref[parents.size()];
      std::copy(parents.begin(), parents.end(), commits.back().parents);
    }
    parents.clear();
  }
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
                                          sha1_ref first_parent, int &max_rev) {
  max_rev = 0;
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
    // Deal with this parent.
    if (base.has_boundary_parents)
      process_future(p);

    new_parents.push_back(p);
    int srev = 0;
    cache.get_rev(p, srev);
    parent_revs.push_back(srev);
    int rev = srev < 0 ? -srev : srev;
    if (rev > max_urev) {
      max_rev = srev;
      max_urev = rev;
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
    // Usually, override the first parent.  However, if this directory has not
    // yet been active on the branch then its original first parent may not be
    // in "first_parent"'s ancestory.
    if (first_parent && i == 0 && dirs.active_dirs.test(source.dir_index))
      continue;

    sha1_ref mono;
    if (cache.get_mono(base.parents[i], mono))
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
  struct tracking_context {
    std::bitset<dir_mask::max_size> added;
    int where[dir_mask::max_size] = {0};
    int parents[dir_mask::max_size] = {0};
  };
  struct other_tracking_context : tracking_context {
    const char *names[dir_mask::max_size];
    int num_names = 0;
    std::bitset<dir_mask::max_size> is_tracked_blob;
  };
  tracking_context dcontext;
  other_tracking_context ocontext;

  // Index by parent.
  constexpr static const size_t max_parents = 128;
  if (parents.size() > max_parents)
    return error(std::to_string(parents.size()) +
                 " is too many parents (max: " + std::to_string(max_parents) +
                 ")");

  auto lookup_other = [&ocontext](const char *name, int &index, bool &is_new) {
    auto &o = ocontext;
    for (int i = 0; i < o.num_names; ++i)
      if (!strcmp(o.names[i], name)) {
        index = i;
        is_new = false;
        return 0;
      }
    if (o.num_names == dir_mask::max_size)
      return 1;
    index = o.num_names++;
    o.names[index] = name;
    is_new = true;
    return 0;
  };

  int base_d = source.dir_index;
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
    if (cache.get_commit_tree(base_commit, base_tree))
      return 1;
    items.emplace_back();
    items.back().sha1 = base_tree;
    items.back().name = dirs.list[base_d].name;
    items.back().type = git_tree::item_type::tree;
  }
  dcontext.added.set(base_d);

  if (is_head && !dirs.active_dirs.test(base_d))
    dirs.active_dirs.set(base_d);

  const bool ignore_blobs = source.is_root;
  bool needs_cleanup = false;
  int blob_parent = -1;
  int untracked_path_parent = -1;
  for (int p = 0, pe = parents.size(); p != pe; ++p) {
    git_tree tree;
    tree.sha1 = parents[p];
    if (cache.ls_tree(tree))
      return 1;

    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      // Pretend "commit" items (for submodules) are also blobs, since they
      // aren't interestingly different and we should treat them similarly.
      const bool is_blob = item.type != git_tree::item_type::tree;
      if (ignore_blobs && is_blob)
        continue;

      bool is_tracked_dir = false;
      int d = dirs.lookup_dir(is_blob ? "-" : item.name, is_tracked_dir);
      if (is_tracked_dir)
        is_tracked_dir = dirs.active_dirs.test(d);
      if (!is_tracked_dir)
        d = -1;

      // The base commit takes priority.
      if (d == base_d)
        continue;

      // Look up context for blobs and untracked trees.
      const bool is_tracked_tree = !is_blob && is_tracked_dir;
      const bool is_tracked_blob = is_blob && is_tracked_dir;
      assert(int(is_tracked_tree) + int(is_tracked_blob) +
                 int(!is_tracked_dir) ==
             1);
      int o = -1;
      if (!is_tracked_tree) {
        bool is_new;
        if (lookup_other(item.name, o, is_new))
          return error("too many blobs and untracked trees");
      }
      if (is_tracked_blob)
        if (blob_parent == -1)
          blob_parent = p;
      if (!is_tracked_dir)
        if (untracked_path_parent == -1)
          untracked_path_parent = p;

      // Add it up front if:
      // - item is a tracked tree not yet seen; or
      // - item is a tracked blob and p is the current blob parent; or
      // - item is untracked and p is the current untracked path parent.
      if (is_tracked_tree) {
        if (!dcontext.added.test(d)) {
          dcontext.where[d] = items.size();
          dcontext.parents[d] = p;
          dcontext.added.set(d);
          items.push_back(item);
          continue;
        }
      } else {
        if ((is_tracked_blob && blob_parent == p) ||
            (!is_tracked_dir && untracked_path_parent == p)) {
          ocontext.where[o] = items.size();
          ocontext.parents[o] = p;
          ocontext.added.set(o);
          ocontext.is_tracked_blob.set(o, is_tracked_blob);
          items.push_back(item);
          continue;
        }
      }

      // The first parent should get caught implicitly by the logic above.
      assert(p > 0);

      // First parent takes priority for tracked dirs.
      if (is_head && is_tracked_dir)
        continue;

      // Look up revs to pick a winner.
      int old_p = is_tracked_tree
                      ? dcontext.parents[d]
                      : is_tracked_blob ? blob_parent : untracked_path_parent;

      // Revs are stored signed, where negative indicates the parent itself is
      // not a commit from upstream LLVM (positive indicates that it is).
      const int old_srev = revs[old_p];
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

      // Handle changes.
      if (is_tracked_tree) {
        // Easy for tracked trees.
        dcontext.parents[d] = p;
        items[dcontext.where[d]] = item;
        continue;
      }

      // Update the parent.
      if (is_tracked_blob)
        blob_parent = p;
      else
        untracked_path_parent = p;

      // Invalidate the old items.
      needs_cleanup = true;
      for (int j = 0; j < ocontext.num_names; ++j)
        if (ocontext.added.test(j))
          if (ocontext.is_tracked_blob.test(j) == is_tracked_blob) {
            items[ocontext.where[j]].sha1 = sha1_ref();
            ocontext.added.reset(j);
          }

      ocontext.where[o] = items.size();
      ocontext.parents[o] = p;
      ocontext.added.set(o);
      ocontext.is_tracked_blob.set(o, is_tracked_blob);
      items.push_back(item);
    }
  }

  if (needs_cleanup)
    items.erase(std::remove_if(
                    items.begin(), items.end(),
                    [](const git_tree::item_type &item) { return !item.sha1; }),
                items.end());

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

  if (!head)
    return 0;

  textual_sha1 sha1(*head);
  printf("%s", sha1.bytes);
  for (auto &dir : dirs.list) {
    if (dir.head)
      sha1 = textual_sha1(*dir.head);
    else
      memset(sha1.bytes, '0', 40);
    printf(" %s:%s", sha1.bytes, dir.name);
  }
  printf("\n");
  return 0;
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
