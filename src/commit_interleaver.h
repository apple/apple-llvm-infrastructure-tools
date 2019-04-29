// commit_interleaver.h
#pragma once

#include "error.h"
#include "git_cache.h"
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
  int parse_source(FILE *file);
};

struct commit_interleaver {
  sha1_pool sha1s;
  git_cache cache;

  sha1_ref head;
  dir_list dirs;
  bool has_root = false;
  translation_queue q;

  commit_interleaver(split2monodb &db, mmapped_file &svn2git)
      : cache(db, svn2git, sha1s, dirs), q(cache, dirs) {
    dirs.list.reserve(64);
  }

  int read_queue_from_stdin();
  void set_head(const textual_sha1 &sha1) { head = sha1s.lookup(sha1); }

  int construct_tree(bool is_head, commit_source &source, sha1_ref base_commit,
                     const std::vector<sha1_ref> &parents,
                     std::vector<git_tree::item_type> &items,
                     sha1_ref &tree_sha1);
  int translate_parents(const commit_type &base,
                        std::vector<sha1_ref> &new_parents,
                        sha1_ref first_parent = sha1_ref());
  int interleave();
  int translate_commit(commit_source &source, const commit_type &base,
                       std::vector<sha1_ref> &new_parents,
                       std::vector<git_tree::item_type> &items,
                       git_cache::commit_tree_buffers &buffers,
                       sha1_ref *head = nullptr);
};
} // end namespace

static int getline(FILE *file, std::string &line) {
  size_t length = 0;
  char *rawline = fgetln(file, &length);
  if (!rawline)
    return 1;
  if (!length)
    return error("unexpected empty line without a newline");
  if (rawline[length - 1] != '\n')
    return error("missing newline at end of file");
  line.assign(rawline, rawline + length - 1);
  return 0;
}

int translation_queue::parse_source(FILE *file) {
  std::string line;
  if (getline(file, line))
    return EOF;
  sources.emplace_back();
  commit_source &source = sources.back();
  size_t space = line.find(' ');
  if (!space || space == std::string::npos || line.compare(0, space, "start"))
    return error("invalid start directive");

  bool found = false;
  int d = dirs.lookup_dir(line.c_str() + space + 1, found);
  if (!found)
    return error("undeclared directory '" + line.substr(space + 1) +
                 "' in start directive");

  source.dir_index = d;
  source.is_root = !strcmp("-", dirs.list[d].name);
  dirs.list[d].source_index = sources.size() - 1;

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

  size_t num_fparents_before = fparents.size();
  int source_index = sources.size() - 1;
  while (!getline(file, line)) {
    if (!line.compare("all"))
      break;

    fparents.emplace_back();
    fparents.back().index = source_index;
    const char *current = line.c_str();
    const char *end = current + line.size();
    if (parse_sha1(current, fparents.back().commit) || parse_space(current) ||
        parse_ct(current, fparents.back().ct))
      return 1;

    if (current != end)
      return error("junk in first-parent line");

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
  while (!getline(file, line)) {
    if (!line.compare("done"))
      break;

    // line ::= commit SP tree ( SP parent )*
    commits.emplace_back();
    const char *current = line.c_str();
    const char *end = current + line.size();
    if (parse_sha1(current, commits.back().commit) || parse_space(current) ||
        parse_sha1(current, commits.back().tree))
      return 1;

    // Warm the cache.
    cache.note_commit_tree(commits.back().commit, commits.back().tree);

    while (!try_parse_space(current)) {
      parents.emplace_back();
      if (parse_sha1(current, parents.back()))
        return error("failed to parse parent");
    }
    commits.back().num_parents = parents.size();
    if (!parents.empty()) {
      commits.back().parents = new (parent_alloc) sha1_ref[parents.size()];
      std::copy(parents.begin(), parents.end(), commits.back().parents);
    }
    parents.clear();

    if (current != end)
      return error("junk in commit line");
  }
  source.commits.count = commits.size() - source.commits.first;
  if (source.commits.count < fparents.size() - num_fparents_before)
    return error("first parents missing from commits");
  return 0;
}

int commit_interleaver::translate_parents(const commit_type &base,
                                          std::vector<sha1_ref> &new_parents,
                                          sha1_ref first_parent) {
  for (int i = 0; i < base.num_parents; ++i) {
    new_parents.emplace_back();
    if (i == 0 && first_parent)
      new_parents.back() = first_parent;
    else if (cache.get_mono(base.parents[i], new_parents.back()))
      return error("parent " + base.parents[i]->to_string() + " of " +
                   base.commit->to_string() + " not translated");
  }
  return 0;
}

int commit_interleaver::construct_tree(bool is_head, commit_source &source,
                                       sha1_ref base_commit,
                                       const std::vector<sha1_ref> &parents,
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
  int revs[max_parents] = {0};
  std::bitset<max_parents> has_rev;
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
  sha1_ref base_tree;
  if (cache.get_commit_tree(base_commit, base_tree))
    return 1;
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
    items.emplace_back();
    items.back().sha1 = base_tree;
    items.back().name = dirs.list[base_d].name;
    items.back().type = git_tree::item_type::tree;
  }
  dcontext.added.set(base_d);

  const bool ignore_blobs = source.is_root;
  bool needs_cleanup = false;
  int blob_parent = -1;
  int untracked_path_parent = -1;
  for (int p = 0, pe = parents.size(); p != pe; ++p) {
    git_tree tree;
    if (cache.get_commit_tree(parents[p], tree.sha1) || cache.ls_tree(tree))
      return 1;

    for (int i = 0; i < tree.num_items; ++i) {
      auto &item = tree.items[i];
      // Pretend "commit" items (for submodules) are also blobs, since they
      // aren't interestingly different and we should treat them similarly.
      const bool is_blob = item.type != git_tree::item_type::tree;
      if (ignore_blobs && is_blob)
        continue;

      bool is_tracked_dir = true;
      int d = dirs.lookup_dir(is_blob ? item.name : "-", is_tracked_dir);

      // The base commit takes priority.
      if (d == base_d)
        continue;

      // Look up context for blobs and untracked trees.
      const bool is_tracked_tree = !is_blob && is_tracked_dir;
      const bool is_tracked_blob = is_blob && is_tracked_dir;
      int o = -1;
      if (!is_tracked_tree) {
        bool is_new;
        if (lookup_other(item.name, o, is_new))
          return error("too many blobs and untracked trees");
      }
      if (is_tracked_blob)
        if (blob_parent == -1)
          blob_parent = p;
      if (!is_tracked_dir) {
        d = -1;
        if (untracked_path_parent == -1)
          untracked_path_parent = p;
      }

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

      // First parent takes priority for tracked dirs if this is in the first
      // parent path (could be head of the branch).
      if (is_head && is_tracked_dir && p > 0)
        continue;

      // Look up revs to pick a winner.
      int old_p = is_tracked_tree
                      ? dcontext.parents[d]
                      : is_tracked_blob ? blob_parent : untracked_path_parent;
      for (int parent : {p, old_p})
        if (!has_rev.test(parent)) {
          if (cache.get_rev(parents[parent], revs[parent]))
            return 1;
          has_rev.set(parent);
        }

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
      for (int i = 0; i < ocontext.num_names; ++i)
        if (ocontext.added.test(i))
          if (ocontext.is_tracked_blob.test(i) == is_tracked_blob) {
            items[ocontext.where[i]].sha1 = sha1_ref();
            ocontext.added.reset(i);
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
  // Construct trees and commit them.
  std::vector<sha1_ref> new_parents;
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
    auto original_last = last;
    while ((--last)->commit != fparent.commit) {
      if (first == last)
        return error("first parent missing from all");
      if (translate_commit(source, *last, new_parents, items, buffers))
        return 1;
    }
    dir.head = last->commit;
    source.commits.count = last - first;
    if (translate_commit(source, *last, new_parents, items, buffers, &head) ||
        report_progress(original_last - last))
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
    std::vector<sha1_ref> &new_parents, std::vector<git_tree::item_type> &items,
    git_cache::commit_tree_buffers &buffers, sha1_ref *head) {
  new_parents.clear();
  items.clear();
  const char *dir = q.dirs.list[source.dir_index].name;
  sha1_ref new_tree, new_commit, first_parent_override;
  if (head)
    first_parent_override = *head;
  if (translate_parents(base, new_parents, first_parent_override) ||
      construct_tree(/*is_head=*/head, source, base.commit, new_parents, items,
                     new_tree) ||
      cache.commit_tree(base.commit, dir, new_tree, new_parents, new_commit,
                        buffers) ||
      cache.set_mono(base.commit, new_commit))
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
  {
    int status = 0;
    while (!status) {
      size_t orig_num_fparents = q.fparents.size();
      status = q.parse_source(stdin);

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
