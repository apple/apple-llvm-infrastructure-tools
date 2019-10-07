// git_cache.h
#pragma once

#include "bisect_first_match.h"
#include "call_git.h"
#include "dir_list.h"
#include "error.h"
#include "parsers.h"
#include "sha1_pool.h"
#include "split2monodb.h"

namespace {
struct git_tree {
  struct item_type {
    enum type_enum {
      unknown,
      tree,
      regular,
      exec,
      symlink,
      submodule,
    };

    sha1_ref sha1;
    const char *name = nullptr;
    type_enum type = unknown;

    constexpr static const char *get_mode(type_enum type);
    constexpr static const char *get_type(type_enum type);
    const char *get_mode() const { return get_mode(type); }
    const char *get_type() const { return get_type(type); }
  };

  git_tree() = default;
  explicit git_tree(const binary_sha1 &sha1) : sha1(&sha1){};
  explicit operator const binary_sha1 &() const { return *sha1; }

  sha1_ref sha1;
  item_type *items = nullptr;
  int num_items = 0;
};

struct git_cache {
  /// Mark the given commit as one that will be translated.  Even if it can't
  /// be found in the commits table, it'll be there eventually.
  void note_being_translated(sha1_ref commit);

  void note_commit_tree(sha1_ref commit, sha1_ref tree);
  void note_mono(sha1_ref split, sha1_ref mono, bool is_based_on_rev);
  void note_rev(sha1_ref commit, int rev);
  void note_tree(const git_tree &tree);
  void note_metadata(sha1_ref commit, const char *metadata, bool is_merge,
                     sha1_ref first_parent);
  void store_metadata_if_new(sha1_ref commit, const char *metadata,
                             const char *metadata_end, bool is_merge,
                             sha1_ref first_parent);
  const char *store_metadata_impl(sha1_ref commit, const char *metadata,
                                  const char *metadata_end, bool is_merge,
                                  sha1_ref first_parent);

  /// Check whether the given commit is going to be (and/or has been)
  /// translated and stored in the commits table.
  bool is_being_translated(sha1_ref commit);

  int lookup_commit_tree(sha1_ref commit, sha1_ref &tree) const;
  int lookup_mono(sha1_ref split, sha1_ref &mono) const;
  int lookup_mono_impl(sha1_ref split, sha1_ref &mono,
                       bool &is_based_on_rev) const;
  int lookup_rev(sha1_ref commit, int &rev) const;
  int lookup_tree(git_tree &tree) const;
  int lookup_metadata(sha1_ref commit, const char *&metadata, bool &is_merge,
                      sha1_ref &first_parent) const;
  int compute_commit_tree(sha1_ref commit, sha1_ref &tree);
  int compute_metadata(sha1_ref commit, const char *&metadata, bool &is_merge,
                       sha1_ref &first_parent);
  int compute_rev(sha1_ref commit, bool is_split, int &rev);
  int compute_rev_with_metadata(sha1_ref commit, bool is_split, int &rev,
                                const char *raw_metadata, bool is_merge,
                                sha1_ref first_parent);
  int parse_for_store_metadata(sha1_ref commit, const char *&metadata,
                               const char *&end_metadata, bool &is_merge,
                               sha1_ref &first_parent);

  /// Add an entry to the svnbaserev table.
  int set_base_rev(sha1_ref commit, int rev);

  /// Figure out the base rev by looking in the svnbaserev table.
  int compute_base_rev(sha1_ref commit, int &rev);

  /// Figure out the monorepo commit without doing any rev-based heroics.  Note
  /// that this still hits the in-memory cache.  If a previous call to
  /// compute_mono cached a result based on the rev, then is_based_on_rev will
  /// be true.
  int compute_mono_from_table(sha1_ref split, sha1_ref &mono,
                              bool &is_based_on_rev);

  /// Compute the monorepo commit, including rev-based heroics.
  int compute_mono(sha1_ref split, sha1_ref &mono);

  int set_mono(sha1_ref split, sha1_ref mono);
  int ls_tree(git_tree &tree);
  int mktree(git_tree &tree);

  int merge_base(sha1_ref a, sha1_ref b, sha1_ref &base);
  int rev_parse(const std::string &rev, sha1_ref &result);
  bool merge_base_is_ancestor(sha1_ref a, sha1_ref b);
  int merge_base_independent(std::vector<sha1_ref> &commits);

  static int ls_tree_impl(sha1_ref sha1, std::vector<char> &reply);
  int note_tree_raw(sha1_ref sha1, const char *rawtree);

  const char *make_name(const char *name, size_t len);
  int parse_name(const char *&current, const char *&name);

  git_tree::item_type *make_items(git_tree::item_type *first,
                                  git_tree::item_type *last);

  struct commit_tree_buffers {
    // These are %cn, %cd, %ce, etc., from `man git-log`.
    std::string cn, cd, ce;
    std::string an, ad, ae;
    std::vector<textual_sha1> parents;
    std::vector<const char *> args;
    std::string message;
  };
  struct parsed_metadata {
    struct string_ref {
      const char *first = nullptr;
      const char *last = nullptr;
      std::string str() const {
        return first == last ? std::string() : std::string(first, last);
      }
      friend bool operator==(const string_ref &lhs, const string_ref &rhs) {
        if (lhs.last - lhs.first != rhs.last - rhs.first)
          return false;
        return strncmp(lhs.first, rhs.first, lhs.last - lhs.first) == 0;
      }
      friend bool operator!=(const string_ref &lhs, const string_ref &rhs) {
        return !(lhs == rhs);
      }
    };

    // These are %cn, %cd, %ce, etc., from `man git-log`.
    string_ref cn, cd, ce;
    string_ref an, ad, ae;
    const char *message = nullptr;
  };
  int commit_tree(sha1_ref base_commit, const dir_type *dir, sha1_ref tree,
                  const std::vector<sha1_ref> &parents, sha1_ref &commit,
                  commit_tree_buffers &buffers, dir_name_range dir_names);
  int commit_tree_impl(sha1_ref tree, const std::vector<sha1_ref> &parents,
                       sha1_ref &commit, commit_tree_buffers &buffers);
  void apply_merge_authorship(commit_tree_buffers &buffers,
                              parsed_metadata::string_ref cd);
  void apply_authorship(commit_tree_buffers &buffers,
                        const parsed_metadata &parsed);
  void apply_dir_names_in_subject(std::string &message,
                                  dir_name_range dir_names);
  void apply_dir_name_trailers(std::string &message, dir_name_range dir_names);
  int extract_subject(std::string &buffer, const char *message);

  static int parse_commit_metadata_impl(const char *metadata,
                                        parsed_metadata &result);
  int parse_commit_metadata(sha1_ref commit,
                            git_cache::commit_tree_buffers &buffers,
                            bool is_merge, dir_name_range dir_names);

  void apply_metadata_env_names(git_cache::commit_tree_buffers &buffers);

  struct sha1_single {
    sha1_ref key;

    explicit sha1_single(const binary_sha1 &sha1) : key(&sha1) {}
    explicit operator const binary_sha1 &() const { return *key; }
  };
  struct sha1_pair {
    sha1_ref key;
    sha1_ref value;

    explicit sha1_pair(const binary_sha1 &sha1) : key(&sha1) {}
    explicit operator const binary_sha1 &() const { return *key; }
  };
  struct split2mono_pair {
    sha1_ref key;
    sha1_ref value;
    bool is_based_on_rev = false;

    explicit split2mono_pair(const binary_sha1 &sha1) : key(&sha1) {}
    explicit operator const binary_sha1 &() const { return *key; }
  };
  struct git_svn_base_rev {
    sha1_ref commit;
    int rev = -1;

    explicit git_svn_base_rev(const binary_sha1 &sha1) : commit(&sha1) {}
    explicit operator const binary_sha1 &() const { return *commit; }
  };

  git_cache(split2monodb &db, mmapped_file &svn2git, sha1_pool &pool,
            dir_list &dirs)
      : db(db), svn2git(svn2git), pool(pool), dirs(dirs) {}

  static constexpr const int num_cache_bits = 20;

  struct sha1_metadata {
    sha1_ref commit;
    const char *metadata = nullptr;
    bool is_merge = false;
    sha1_ref first_parent;

    explicit sha1_metadata(const binary_sha1 &sha1) : commit(&sha1) {}
    explicit operator const binary_sha1 &() const { return *commit; }
  };

  sha1_trie<git_tree> trees;
  sha1_trie<sha1_pair> commit_trees;
  sha1_trie<git_svn_base_rev> revs;
  sha1_trie<split2mono_pair> monos;
  sha1_trie<sha1_metadata> metadata;
  sha1_trie<sha1_single> being_translated;

  std::vector<const char *> names;
  std::vector<std::unique_ptr<char[]>> big_metadata;

  bump_allocator name_alloc;
  bump_allocator tree_item_alloc;
  split2monodb &db;
  mmapped_file &svn2git;
  sha1_pool &pool;
  dir_list &dirs;
  std::vector<char> git_reply;
  std::string git_input;
};
} // end namespace

constexpr const char *git_tree::item_type::get_mode(type_enum type) {
  switch (type) {
  default:
    return nullptr;
  case tree:
    return "040000";
  case regular:
    return "100644";
  case exec:
    return "100755";
  case symlink:
    return "120000";
  case submodule:
    return "160000";
  }
}

constexpr const char *git_tree::item_type::get_type(type_enum type) {
  switch (type) {
  default:
    return nullptr;
  case tree:
    return "tree";
  case regular:
    return "blob";
  case exec:
    return "blob";
  case symlink:
    return "blob";
  case submodule:
    return "commit";
  }
  return nullptr;
}

void git_cache::note_being_translated(sha1_ref commit) {
  assert(commit);
  bool was_inserted = false;
  sha1_single *inserted = being_translated.insert(*commit, was_inserted);
  assert(inserted);
  assert(was_inserted);
}

void git_cache::note_commit_tree(sha1_ref commit, sha1_ref tree) {
  assert(tree);
  bool was_inserted = false;
  sha1_pair *inserted = commit_trees.insert(*commit, was_inserted);
  assert(inserted);
  inserted->value = tree;
}

void git_cache::note_rev(sha1_ref commit, int rev) {
  bool was_inserted = false;
  git_svn_base_rev *inserted = revs.insert(*commit, was_inserted);
  assert(inserted);
  inserted->rev = rev;
}

void git_cache::note_mono(sha1_ref split, sha1_ref mono, bool is_based_on_rev) {
  assert(mono);
  bool was_inserted = false;
  split2mono_pair *inserted = monos.insert(*split, was_inserted);
  assert(inserted);
  inserted->value = mono;
  inserted->is_based_on_rev = is_based_on_rev;
}

void git_cache::note_tree(const git_tree &tree) {
  bool was_inserted = false;
  git_tree *inserted = trees.insert(*tree.sha1, was_inserted);
  assert(inserted);
  assert(inserted->sha1 == tree.sha1);
  *inserted = tree;
}

void git_cache::note_metadata(sha1_ref commit, const char *metadata,
                              bool is_merge, sha1_ref first_parent) {
  bool was_inserted = false;
  sha1_metadata *inserted = this->metadata.insert(*commit, was_inserted);
  assert(inserted);
  inserted->metadata = metadata;
  inserted->is_merge = is_merge;
  inserted->first_parent = first_parent;
}

bool git_cache::is_being_translated(sha1_ref commit) {
  return being_translated.lookup(*commit);
}

int git_cache::lookup_commit_tree(sha1_ref commit, sha1_ref &tree) const {
  sha1_pair *existing = commit_trees.lookup(*commit);
  if (!existing)
    return 1;
  tree = existing->value;
  return 0;
}

int git_cache::lookup_rev(sha1_ref commit, int &rev) const {
  git_svn_base_rev *existing = revs.lookup(*commit);
  if (!existing)
    return 1;
  rev = existing->rev;
  return 0;
}

int git_cache::lookup_mono(sha1_ref split, sha1_ref &mono) const {
  bool is_based_on_rev;
  return lookup_mono_impl(split, mono, is_based_on_rev);
}
int git_cache::lookup_mono_impl(sha1_ref split, sha1_ref &mono,
                                bool &is_based_on_rev) const {
  split2mono_pair *existing = monos.lookup(*split);
  if (!existing)
    return 1;
  mono = existing->value;
  is_based_on_rev = existing->is_based_on_rev;
  return 0;
}

int git_cache::lookup_tree(git_tree &tree) const {
  git_tree *existing = trees.lookup(*tree.sha1);
  if (!existing)
    return 1;
  tree = *existing;
  return 0;
}

int git_cache::lookup_metadata(sha1_ref commit, const char *&metadata,
                               bool &is_merge, sha1_ref &first_parent) const {
  sha1_metadata *existing = this->metadata.lookup(*commit);
  if (!existing)
    return 1;
  metadata = existing->metadata;
  is_merge = existing->is_merge;
  first_parent = existing->first_parent;
  return 0;
}

int git_cache::set_mono(sha1_ref split, sha1_ref mono) {
  if (commits_query(*split).insert_data(db.commits, *mono))
    return error("failed to map split " + split->to_string() + " to mono " +
                 mono->to_string());
  note_mono(split, mono, /*is_based_on_rev=*/false);
  return 0;
}

int git_cache::compute_mono_from_table(sha1_ref split, sha1_ref &mono,
                                       bool &is_based_on_rev) {
  if (!lookup_mono_impl(split, mono, is_based_on_rev))
    return 0;

  is_based_on_rev = false;
  binary_sha1 sha1;
  if (commits_query(*split).lookup_data(db.commits, sha1))
    return 1;

  mono = pool.lookup(sha1);
  note_mono(split, mono, /*is_based_on_rev=*/false);
  return 0;
}

int git_cache::compute_mono(sha1_ref split, sha1_ref &mono) {
  bool is_based_on_rev = false;
  if (!compute_mono_from_table(split, mono, is_based_on_rev))
    return 0;

  int rev = -1;
  if (compute_rev(split, /*is_split=*/true, rev) || rev <= 0)
    return 1;

  // This looks like a real git-svn commit.  Even so, there may not be a
  // monorepo commit for it.  Not all historical branches got translated.
  // Unfortunately this makes it impossible to differentiate "not mapped" and
  // "no monorepo commit" from here.
  auto *bytes = reinterpret_cast<const unsigned char *>(svn2git.bytes);
  long offset = 20 * rev;
  if (offset + 20 > svn2git.num_bytes)
    return 1;

  binary_sha1 sha1;
  sha1.from_binary(bytes + offset);
  mono = pool.lookup(sha1);
  if (!mono)
    return 1;
  note_mono(split, mono, /*is_based_on_rev=*/true);
  note_rev(mono, rev);
  return 0;
}

int git_cache::compute_commit_tree(sha1_ref commit, sha1_ref &tree) {
  if (!lookup_commit_tree(commit, tree))
    return 0;

  assert(commit);
  std::string ref = textual_sha1(*commit).bytes;
  ref += "^{tree}";
  const char *argv[] = {"git", "rev-parse", "--verify", ref.c_str(), nullptr};
  git_reply.clear();
  if (call_git(argv, nullptr, "", git_reply))
    return 1;

  git_reply.push_back(0);
  const char *end = nullptr;
  textual_sha1 text;
  if (text.from_input(&git_reply[0], &end) || *end++ != '\n' || *end)
    return 1;

  note_commit_tree(commit, pool.lookup(text));
  return 0;
}

int git_cache::compute_metadata(sha1_ref commit, const char *&metadata,
                                bool &is_merge, sha1_ref &first_parent) {
  if (!lookup_metadata(commit, metadata, is_merge, first_parent))
    return 0;

  textual_sha1 sha1(*commit);
  const char *args[] = {
      "git",
      "log",
      "--date=raw",
      "--no-walk",
      "--format=%P%x00%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B%x00",
      sha1.bytes,
      nullptr,
  };
  git_reply.clear();
  if (call_git(args, nullptr, "", git_reply))
    return error(std::string("failed to read commit metadata for ") +
                 sha1.bytes);
  if (git_reply.empty())
    return error("missing commit metadata for " + sha1.to_string());

  git_reply.push_back(0);

  metadata = git_reply.data();
  const char *end_metadata = metadata + git_reply.size() - 1;
  if (parse_for_store_metadata(commit, metadata, end_metadata, is_merge,
                               first_parent))
    return 1;
  metadata = store_metadata_impl(commit, metadata, end_metadata, is_merge,
                                 first_parent);
  return 0;
}

int git_cache::parse_for_store_metadata(sha1_ref commit, const char *&metadata,
                                        const char *&end_metadata,
                                        bool &is_merge,
                                        sha1_ref &first_parent) {
  first_parent = sha1_ref();
  is_merge = false;

  // Parse the parents to fill in is_merge and first_parent.
  auto parse_parents = [&]() {
    // Check for a root commit.
    if (!parse_null(metadata))
      return 0;

    if (pool.parse_sha1(metadata, first_parent))
      return error("invalid first parent for " + commit->to_string());

    if (!parse_space(metadata))
      is_merge = true;
    return parse_through_null(metadata, end_metadata);
  };
  if (parse_parents())
    return error("failed to parse parents before metadata for '" +
                 commit->to_string() + "'");

  // Refine the end.
  end_metadata = metadata;
  skip_until_null(end_metadata);
  return 0;
}

void git_cache::store_metadata_if_new(sha1_ref commit, const char *metadata,
                                      const char *metadata_end, bool is_merge,
                                      sha1_ref first_parent) {
  // Check if we already have it, to avoid allocating again.
  if (sha1_metadata *existing = this->metadata.lookup(*commit))
    return;

  (void)store_metadata_impl(commit, metadata, metadata_end, is_merge,
                            first_parent);
}

const char *git_cache::store_metadata_impl(sha1_ref commit,
                                           const char *metadata,
                                           const char *metadata_end,
                                           bool is_merge,
                                           sha1_ref first_parent) {
  // Save the rest.
  char *storage = nullptr;
  auto metadata_size = metadata_end - metadata;
  if (metadata_size >= 4096) {
    big_metadata.emplace_back(new char[metadata_size + 1]);
    storage = big_metadata.back().get();
  } else {
    storage =
        new (name_alloc.allocate(metadata_size + 1, 1)) char[metadata_size + 1];
  }
  memcpy(storage, metadata, metadata_size);
  storage[metadata_size] = 0;
  note_metadata(commit, storage, is_merge, first_parent);
  return storage;
}

int git_cache::set_base_rev(sha1_ref commit, int rev) {
  // We expect this to always be negative, since llvm.org upstream commits
  // don't get mapped.
  if (rev > 0)
    return error("unexpected upstream mapping from r" + std::to_string(rev) +
                 " to " + commit->to_string());

  // It's a little unfortunate to be storing something that is never positive
  // as a negative number, but a long-standing bug means that existing
  // databases have negative numbers in them.  It's not clear there's good
  // motivation to change now.
  svnbaserev dbrev;
  dbrev.set_rev(rev);
  if (svnbase_query(*commit).insert_data(db.svnbase, dbrev))
    return error("failed to map commit " + commit->to_string() + " to rev " +
                 std::to_string(rev));
  note_rev(commit, rev);
  return 0;
}

int git_cache::compute_rev(sha1_ref commit, bool is_split, int &rev) {
  return compute_rev_with_metadata(commit, is_split, rev, nullptr, false,
                                   sha1_ref());
}

int git_cache::compute_base_rev(sha1_ref commit, int &rev) {
  if (!lookup_rev(commit, rev))
    return 0;

  svnbaserev dbrev;
  if (svnbase_query(*commit).lookup_data(db.svnbase, dbrev))
    return 1;

  // We expect this to always be negative, since llvm.org upstream commits
  // don't get mapped.
  rev = dbrev.get_rev();
  note_rev(commit, rev);
  return 0;
}

int git_cache::compute_rev_with_metadata(sha1_ref commit, bool is_split,
                                         int &rev, const char *raw_metadata,
                                         bool is_merge, sha1_ref first_parent) {
  if (is_split) {
    // Split commits aren't mapped in the svnbaserev table.
    if (!lookup_rev(commit, rev))
      return 0;
  } else {
    // Starts with a call to lookup_rev.
    if (!compute_base_rev(commit, rev))
      return 0;
  }

  if (!raw_metadata)
    if (compute_metadata(commit, raw_metadata, is_merge, first_parent))
      return 1;

  // Merges cannot be upstream SVN commits.
  if (is_merge)
    return 1;

  if (is_split && first_parent) {
    // If first_parent is in the split2mono database, then commit is not an
    // upstream SVN commit.  This helps to avoid mapping commits like
    // 8bf1494af87f222db2b637a3be6cee40a9a51a62 from swift-clang to commit
    // being cherry-picked (in this case, r354826), since the cherry-pick's
    // parent will not be upstream.
    if (is_being_translated(first_parent))
      return 1;
    sha1_ref mono;
    bool is_based_on_rev = false;
    if (!compute_mono_from_table(first_parent, mono, is_based_on_rev))
      if (!is_based_on_rev)
        return 1;
  }

  parsed_metadata parsed;
  if (parse_commit_metadata_impl(raw_metadata, parsed))
    return 1;

  // Check that committer and author match.
  if (parsed.an != parsed.cn || parsed.ae != parsed.ce ||
      parsed.ad != parsed.cd)
    return 1;

  const char *current = parsed.message;
  while (*current) {
    if (!is_split) {
      if (try_parse_string(current, "llvm-rev: "))
        continue;

      int parsed_rev;
      if (parse_num(current, parsed_rev) || parse_ch(current, '\n'))
        break;
      rev = parsed_rev;
      note_rev(commit, rev);
      return 0;
    }

    if (try_parse_string(current,
                         "git-svn-id: https://llvm.org/svn/llvm-project/")) {
      skip_until(current, '\n');
      parse_ch(current, '\n');
      continue;
    }

    int parsed_rev;
    if (skip_until(current, '@') || parse_ch(current, '@') ||
        parse_num(current, parsed_rev) || parse_ch(current, ' '))
      break;

    rev = parsed_rev;
    note_rev(commit, rev);
    return 0;
  }

  // Monorepo commits should always have a rev, either from the 'llvm-rev:' tag
  // or stored in the svnbaserev table.  If we get here there's a problem.
  if (!is_split)
    return error("missing base svn rev for monorepo commit");

  // This is a split commit that's not an upstream commit.
  rev = 0;
  note_rev(commit, rev);
  return 0;
}

const char *git_cache::make_name(const char *name, size_t len) {
  bool found = false;
  auto d = bisect_first_match(dirs.list.begin(), dirs.list.end(),
                              [&name, &found](const dir_type &dir) {
                                int diff = strcmp(name, dir.name);
                                found |= !diff;
                                return diff <= 0;
                              });
  assert(!found || d != dirs.list.end());
  if (found)
    return d->name;

  auto n = bisect_first_match(names.begin(), names.end(),
                              [&name, &found](const char *x) {
                                int diff = strcmp(name, x);
                                found |= !diff;
                                return diff <= 0;
                              });
  assert(!found || n != names.end());
  if (found)
    return *n;
  char *allocated = new (name_alloc) char[len + 1];
  strncpy(allocated, name, len);
  allocated[len] = 0;
  return *names.insert(n, allocated);
}

git_tree::item_type *git_cache::make_items(git_tree::item_type *first,
                                           git_tree::item_type *last) {
  if (first == last)
    return nullptr;
  auto *items = new (tree_item_alloc) git_tree::item_type[last - first];
  std::move(first, last, items);
  return items;
}

int git_cache::ls_tree(git_tree &tree) {
  if (!lookup_tree(tree))
    return 0;

  // Check if this is a commit whose tree we have, likely because we built it.
  sha1_ref tree_sha1;
  if (!lookup_commit_tree(tree.sha1, tree_sha1)) {
    sha1_ref commit = tree.sha1;
    tree.sha1 = tree_sha1;
    int status = lookup_tree(tree);
    tree.sha1 = commit;
    if (!status)
      return 0;
  }

  if (ls_tree_impl(tree.sha1, git_reply) ||
      note_tree_raw(tree.sha1, git_reply.data()))
    return 1;
  if (lookup_tree(tree))
    return error("internal: noted tree not found");
  return 0;
}

int git_cache::ls_tree_impl(sha1_ref sha1, std::vector<char> &git_reply) {
  assert(!sha1->is_zeros());
  std::string ref = sha1->to_string();
  const char *args[] = {"git", "ls-tree", "--full-tree", ref.c_str(), nullptr};
  git_reply.clear();
  if (call_git(args, nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);
  return 0;
}

int git_cache::note_tree_raw(sha1_ref sha1, const char *rawtree) {
  auto parse_mode = [](const char *&current, auto &type) {
    assert(type == git_tree::item_type::unknown);
#define PARSE_MODE_FOR_TYPE(VALUE)                                             \
  PARSE_MODE_FOR_TYPE_IMPL(                                                    \
      git_tree::item_type::get_mode(git_tree::item_type::VALUE), VALUE)
#define PARSE_MODE_FOR_TYPE_IMPL(PATTERN, VALUE)                               \
  do {                                                                         \
    if (!strncmp(current, PATTERN, strlen(PATTERN))) {                         \
      type = git_tree::item_type::VALUE;                                       \
      current += strlen(PATTERN);                                              \
      return 0;                                                                \
    }                                                                          \
  } while (false)

    PARSE_MODE_FOR_TYPE(tree);
    PARSE_MODE_FOR_TYPE(regular);
    PARSE_MODE_FOR_TYPE(exec);
    PARSE_MODE_FOR_TYPE(symlink);
    PARSE_MODE_FOR_TYPE(submodule);
#undef PARSE_MODE_FOR_TYPE
#undef PARSE_MODE_FOR_TYPE_IMPL
    return 1;
  };
  auto parse_type = [](const char *&current, auto type) {
    assert(type != git_tree::item_type::unknown);
#define PARSE_TYPE_AND_CHECK(VALUE)                                            \
  PARSE_TYPE_AND_CHECK_IMPL(                                                   \
      git_tree::item_type::get_type(git_tree::item_type::VALUE), VALUE)
#define PARSE_TYPE_AND_CHECK_IMPL(PATTERN, VALUE)                              \
  do                                                                           \
    if (!strncmp(current, PATTERN, strlen(PATTERN)))                           \
      if (type == git_tree::item_type::VALUE) {                                \
        current += strlen(PATTERN);                                            \
        return 0;                                                              \
      }                                                                        \
  while (false)
    PARSE_TYPE_AND_CHECK(tree);
    PARSE_TYPE_AND_CHECK(regular);
    PARSE_TYPE_AND_CHECK(exec);
    PARSE_TYPE_AND_CHECK(symlink);
    PARSE_TYPE_AND_CHECK(submodule);
#undef PARSE_TYPE_AND_CHECK
#undef PARSE_TYPE_AND_CHECK_IMPL
    return 1;
  };

  constexpr const int max_items = dir_mask::max_size;
  git_tree::item_type items[max_items];
  git_tree::item_type *last = items;
  const char *current = rawtree;
  while (*current) {
    if (last - items == max_items)
      return error(
          "ls-tree: too many items (max: " + std::to_string(max_items) + ")");

    if (parse_mode(current, last->type) || parse_ch(current, ' ') ||
        parse_type(current, last->type) || parse_ch(current, ' ') ||
        pool.parse_sha1(current, last->sha1) || parse_ch(current, '\t') ||
        parse_name(current, last->name) || parse_ch(current, '\n'))
      return error("ls-tree: could not parse entry");
    ++last;
  }

  git_tree tree;
  tree.sha1 = sha1;
  tree.num_items = last - items;
  tree.items = make_items(items, last);
  note_tree(tree);
  return 0;
}

int git_cache::parse_name(const char *&current, const char *&name) {
  const char *ch = current;
  for (; *ch; ++ch)
    if (*ch == '\n') {
      name = make_name(current, ch - current);
      current = ch;
      return 0;
    }
  return 1;
}

int git_cache::mktree(git_tree &tree) {
  assert(!tree.sha1);

  git_input.clear();
  git_input.reserve(tree.num_items *
                    (sizeof("tree") + sizeof("100644") + sizeof(textual_sha1) +
                     sizeof("somedirname") + sizeof("  \t\n")));
  for (auto i = 0; i != tree.num_items; ++i) {
    assert(tree.items[i].sha1);
    git_input += tree.items[i].get_mode();
    git_input += ' ';
    git_input += tree.items[i].get_type();
    git_input += ' ';
    git_input += textual_sha1(*tree.items[i].sha1).bytes;
    git_input += '\t';
    git_input += tree.items[i].name;
    git_input += '\n';
  }

  const char *argv[] = {"git", "mktree", nullptr};
  git_reply.clear();
  if (call_git(argv, nullptr, git_input, git_reply))
    return 1;
  git_reply.push_back(0);

  textual_sha1 text;
  const char *end = nullptr;
  if (text.from_input(&git_reply[0], &end) || *end++ != '\n' || *end)
    return 1;

  tree.sha1 = pool.lookup(text);
  note_tree(tree);
  return 0;
}

bool git_cache::merge_base_is_ancestor(sha1_ref a, sha1_ref b) {
  // Could use 'git merge-base --is-ancestor', but this is easier to type.
  sha1_ref base;
  if (merge_base(a, b, base))
    return false;
  return base == a;
}

int git_cache::merge_base(sha1_ref a, sha1_ref b, sha1_ref &base) {
  assert(a);
  assert(b);
  assert(!base);

  textual_sha1 a_text(*a);
  textual_sha1 b_text(*b);
  const char *argv[] = {"git", "merge-base", a_text.bytes, b_text.bytes,
                        nullptr};
  git_reply.clear();
  if (call_git(argv, nullptr, "", git_reply))
    return 1;
  git_reply.push_back(0);

  textual_sha1 base_text;
  const char *end = nullptr;
  if (base_text.from_input(&git_reply[0], &end) || *end++ != '\n' || *end)
    return 1;

  // Doesn't seem like we need a cache for the response; just put the SHA-1 in
  // the pool and return.
  base = pool.lookup(base_text);
  return 0;
}

int git_cache::rev_parse(const std::string &rev, sha1_ref &result) {
  const char *argv[] = {"git", "rev-parse", "--verify", rev.c_str(), nullptr};
  git_reply.clear();
  if (call_git(argv, nullptr, "", git_reply, /*ignore_errors=*/true))
    return 1;
  git_reply.push_back(0);

  textual_sha1 base_text;
  const char *end = nullptr;
  if (base_text.from_input(&git_reply[0], &end) || *end++ != '\n' || *end)
    return 1;

  result = pool.lookup(base_text);
  return 0;
}

int git_cache::merge_base_independent(std::vector<sha1_ref> &commits) {
  // Fill these first to avoid memory corruption.
  std::vector<textual_sha1> sha1s;
  for (auto &sha1 : commits)
    sha1s.emplace_back(*sha1);
  commits.clear();

  std::vector<const char *> argv;
  argv.push_back("git");
  argv.push_back("merge-base");
  argv.push_back("--independent");
  for (auto &sha1 : sha1s)
    argv.push_back(sha1.bytes);
  argv.push_back(nullptr);

  git_reply.clear();
  if (call_git(argv.data(), nullptr, "", git_reply))
    return 1;

  git_reply.push_back(0);
  const char *current = git_reply.data();
  while (*current) {
    textual_sha1 text;
    if (text.from_input(current, &current) || *current++ != '\n')
      return 1;

    commits.push_back(pool.lookup(text));
  }
  return 0;
}

int git_cache::parse_commit_metadata_impl(const char *metadata,
                                          parsed_metadata &result) {
  auto skip_until = [](const char *&current, int ch) {
    for (; *current; ++current)
      if (*current == ch)
        return 0;
    return 1;
  };
  auto parse_suffix = [&skip_until](const char *&current,
                                    parsed_metadata::string_ref &s) {
    s.first = current;
    if (skip_until(current, '\n'))
      return 1;
    s.last = current;
    ++current;
    return 0;
  };
  if (parse_suffix(metadata, result.an) || parse_suffix(metadata, result.cn) ||
      parse_suffix(metadata, result.ad) || parse_suffix(metadata, result.cd) ||
      parse_suffix(metadata, result.ae) || parse_suffix(metadata, result.ce))
    return error("failed to parse commit metadata");

  result.message = metadata;
  return 0;
}

void git_cache::apply_metadata_env_names(git_cache::commit_tree_buffers &buffers) {
  const char *prefixes[] = {
      "GIT_AUTHOR_NAME=",    "GIT_COMMITTER_NAME=", "GIT_AUTHOR_DATE=",
      "GIT_COMMITTER_DATE=", "GIT_AUTHOR_EMAIL=",   "GIT_COMMITTER_EMAIL="};
  std::string *vars[] = {&buffers.an, &buffers.cn, &buffers.ad,
                         &buffers.cd, &buffers.ae, &buffers.ce};
  for (int i = 0; i < 6; ++i)
    *vars[i] = prefixes[i];
}

void git_cache::apply_dir_names_in_subject(std::string &message,
                                           dir_name_range dir_names) {
  for (int i = 0, ie = dir_names.end() - dir_names.begin(); i != ie; ++i) {
    const char *name = dir_names.begin()[i];
    if (i) {
      if (ie == 2)
        message += " and ";
      else if (i + 1 == ie)
        message += ", and ";
      else
        message += ", ";
    }
    message += strcmp(name, "-") ? name : "root";
  }
}

static void append_split_dir_trailer(std::string &message, const char *name) {
  message += "apple-llvm-split-dir: ";
  message += name;
  if (strcmp("-", name))
    message += '/';
  message += '\n';
}

void git_cache::apply_dir_name_trailers(std::string &message,
                                        dir_name_range dir_names) {
  for (auto i = dir_names.begin(), ie = dir_names.end(); i != ie; ++i)
    append_split_dir_trailer(message, *i);
}

int git_cache::parse_commit_metadata(sha1_ref commit,
                                     git_cache::commit_tree_buffers &buffers,
                                     bool is_merge, dir_name_range dir_names) {
  apply_metadata_env_names(buffers);
  parsed_metadata parsed;
  {
    const char *metadata;
    bool is_merge;
    sha1_ref first_parent;
    if (compute_metadata(commit, metadata, is_merge, first_parent) ||
        parse_commit_metadata_impl(metadata, parsed))
      return 1;
  }

  if (is_merge)
    apply_merge_authorship(buffers, parsed.cd);
  else
    apply_authorship(buffers, parsed);

  if (!is_merge) {
    buffers.message = parsed.message;
    return 0;
  }

  buffers.message = "Merge ";
  apply_dir_names_in_subject(buffers.message, dir_names);
  buffers.message += ": ";
  if (extract_subject(buffers.message, parsed.message))
    return error("failed to extract subject from '" + commit->to_string() +
                 "'");

  // Account for an empty subject.
  if (buffers.message.back() != '\n')
    buffers.message += '\n';
  buffers.message += '\n';
  apply_dir_name_trailers(buffers.message, dir_names);
  return 0;
}

void git_cache::apply_merge_authorship(commit_tree_buffers &buffers,
                                       parsed_metadata::string_ref cd) {
  buffers.an.append("apple-llvm-mt");
  buffers.cn.append("apple-llvm-mt");
  buffers.ae.append("mt @ apple-llvm");
  buffers.ce.append("mt @ apple-llvm");
  buffers.ad.append(cd.first, cd.last);
  buffers.cd.append(cd.first, cd.last);
}

void git_cache::apply_authorship(commit_tree_buffers &buffers,
                                 const parsed_metadata &parsed) {
  buffers.an.append(parsed.an.first, parsed.an.last);
  buffers.cn.append(parsed.cn.first, parsed.cn.last);
  buffers.ae.append(parsed.ae.first, parsed.ae.last);
  buffers.ce.append(parsed.ce.first, parsed.ce.last);
  buffers.ad.append(parsed.ad.first, parsed.ad.last);
  buffers.cd.append(parsed.cd.first, parsed.cd.last);
}

int git_cache::extract_subject(std::string &buffer,
                               const char *message) {
  // For merge commits, just extract the subject.
  const char *start = message;
  const char *body = start;
  auto skip_until = [](const char *&current, int ch) {
    for (; *current; ++current)
      if (*current == ch)
        return 0;
    return 1;
  };
  while (!skip_until(body, '\n'))
    if (*++body == '\n')
      break;
  buffer.append(start, body);
  return 0;
}

static int num_newlines_before_trailers(const std::string &message) {
  // Check for an empty message.
  if (message.empty())
    return 0;

  // Check if the subject ends.
  const char *first = message.c_str();
  const char *last = first + message.size();
  assert(*last == 0);
  if (*--last != '\n')
    return 2;

  // Pattern match for a suffix of trailers.
  bool newline = true;
  bool space = false;
  bool colon = false;
  bool in_trailer = false;
  while (first != last) {
    int ch = *--last;
    if (ch == '\n') {
      if (newline)
        return 0;
      if (!in_trailer)
        return 1;
      newline = true;
      in_trailer = false;
      continue;
    }
    newline = false;

    if (ch == ' ') {
      // Line matches "* *\n";
      space = true;
      colon = false;
      in_trailer = false;
      continue;
    }
    if (ch == ':') {
      // Line matches "*: *\n";
      if (colon) {
        colon = false;
        continue;
      }
      if (space)
        colon = true;
      space = false;
      in_trailer = false;
      continue;
    }

    space = false;
    if (!in_trailer && !colon)
      continue;
    colon = false;

    // Line matches "*: *\n".
    in_trailer = true;
    if (ch >= 'a' && ch <= 'z')
      continue;
    if (ch >= 'Z' && ch <= 'Z')
      continue;
    if (ch >= '0' && ch <= '9')
      continue;
    if (ch == '_' || ch == '-' || ch == '+')
      continue;
    in_trailer = false;
  }

  // The subject is never a trailer.
  return 1;
}

static void append_trailers(const char *dir, sha1_ref base_commit,
                            std::string &message) {
  // If this is a "repeat" merge, probably we don't need a trailer.
  if (!dir)
    return;

  for (int i = 0, ie = num_newlines_before_trailers(message); i != ie; ++i)
    message += '\n';
  textual_sha1 sha1(*base_commit);
  message += "apple-llvm-split-commit: ";
  message += sha1.bytes;
  message += '\n';
  append_split_dir_trailer(message, dir);
}

int git_cache::commit_tree(
    sha1_ref base_commit, const dir_type *dir, sha1_ref tree,
    const std::vector<sha1_ref> &parents, sha1_ref &commit,
    commit_tree_buffers &buffers, dir_name_range dir_names) {
  if (parse_commit_metadata(base_commit, buffers, /*is_merge=*/!dir,
                            dir_names))
    return error("failed to get metadata for " + base_commit->to_string());
  append_trailers(dir ? dir->name : nullptr, base_commit, buffers.message);
  return commit_tree_impl(tree, parents, commit, buffers);
}

int git_cache::commit_tree_impl(sha1_ref tree,
                                const std::vector<sha1_ref> &parents,
                                sha1_ref &commit,
                                commit_tree_buffers &buffers) {
  const char *envp[] = {buffers.an.c_str(),
                        buffers.ae.c_str(),
                        buffers.ad.c_str(),
                        buffers.cn.c_str(),
                        buffers.ce.c_str(),
                        buffers.cd.c_str(),
                        nullptr};

  buffers.parents.clear();
  for (sha1_ref p : parents)
    buffers.parents.emplace_back(*p);

  textual_sha1 text_tree(*tree);
  buffers.args.clear();
  buffers.args.push_back("git");
  buffers.args.push_back("commit-tree");
  buffers.args.push_back("-F");
  buffers.args.push_back("-");
  buffers.args.push_back(text_tree.bytes);
  for (auto &p : buffers.parents) {
    buffers.args.push_back("-p");
    buffers.args.push_back(p.bytes);
  }
  buffers.args.push_back(nullptr);

  git_reply.clear();
  if (call_git(buffers.args.data(), envp, buffers.message, git_reply))
    return 1;
  git_reply.push_back(0);

  textual_sha1 sha1;
  const char *end = nullptr;
  if (sha1.from_input(git_reply.data(), &end) || *end++ != '\n' || *end)
    return error("invalid sha1 for new commit");
  commit = pool.lookup(sha1);
  note_commit_tree(commit, tree);
  return 0;
}
