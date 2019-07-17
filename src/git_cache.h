// git_cache.h
#pragma once

#include "bisect_first_match.h"
#include "call_git.h"
#include "dir_list.h"
#include "error.h"
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
  void note_commit_tree(sha1_ref commit, sha1_ref tree);
  void note_mono(sha1_ref split, sha1_ref mono);
  void note_rev(sha1_ref commit, int rev);
  void note_tree(const git_tree &tree);
  void note_metadata(sha1_ref commit, const char *metadata);
  int lookup_commit_tree(sha1_ref commit, sha1_ref &tree) const;
  int lookup_mono(sha1_ref split, sha1_ref &mono) const;
  int lookup_rev(sha1_ref commit, int &rev) const;
  int lookup_tree(git_tree &tree) const;
  int lookup_metadata(sha1_ref commit, const char *&metadata) const;
  int compute_commit_tree(sha1_ref commit, sha1_ref &tree);
  int compute_metadata(sha1_ref commit, const char *&metadata);
  int compute_rev(sha1_ref commit, int &rev);
  int compute_rev_with_metadata(sha1_ref commit, int &rev,
                                const char *raw_metadata);
  int set_rev(sha1_ref commit, int rev);
  int compute_mono(sha1_ref split, sha1_ref &mono);
  int set_mono(sha1_ref split, sha1_ref mono);
  int ls_tree(git_tree &tree);
  int mktree(git_tree &tree);

  static int ls_tree_impl(sha1_ref sha1, std::vector<char> &reply);
  int note_tree_raw(sha1_ref sha1, const char *rawtree);

  const char *make_name(const char *name, size_t len);
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
  int commit_tree(sha1_ref base_commit, const char *dir, sha1_ref tree,
                  const std::vector<sha1_ref> &parents, sha1_ref &commit,
                  commit_tree_buffers &buffers);
  static int parse_commit_metadata_impl(const char *metadata,
                                        parsed_metadata &result);
  int parse_commit_metadata(sha1_ref commit,
                            git_cache::commit_tree_buffers &buffers,
                            bool is_merge);

  struct sha1_pair {
    sha1_ref key;
    sha1_ref value;

    explicit sha1_pair(const binary_sha1 &sha1) : key(&sha1) {}
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
    const char *metadata;

    explicit sha1_metadata(const binary_sha1 &sha1) : commit(&sha1) {}
    explicit operator const binary_sha1 &() const { return *commit; }
  };

  sha1_trie<git_tree> trees;
  sha1_trie<sha1_pair> commit_trees;
  sha1_trie<git_svn_base_rev> revs;
  sha1_trie<sha1_pair> monos;
  sha1_trie<sha1_metadata> metadata;

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

void git_cache::note_mono(sha1_ref split, sha1_ref mono) {
  assert(mono);
  bool was_inserted = false;
  sha1_pair *inserted = monos.insert(*split, was_inserted);
  assert(inserted);
  inserted->value = mono;
}

void git_cache::note_tree(const git_tree &tree) {
  bool was_inserted = false;
  git_tree *inserted = trees.insert(*tree.sha1, was_inserted);
  assert(inserted);
  assert(inserted->sha1 == tree.sha1);
  *inserted = tree;
}

void git_cache::note_metadata(sha1_ref commit, const char *metadata) {
  bool was_inserted = false;
  sha1_metadata *inserted = this->metadata.insert(*commit, was_inserted);
  assert(inserted);
  inserted->metadata = metadata;
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
  sha1_pair *existing = monos.lookup(*split);
  if (!existing)
    return 1;
  mono = existing->value;
  return 0;
}

int git_cache::lookup_tree(git_tree &tree) const {
  git_tree *existing = trees.lookup(*tree.sha1);
  if (!existing)
    return 1;
  tree = *existing;
  return 0;
}

int git_cache::lookup_metadata(sha1_ref commit, const char *&metadata) const {
  sha1_metadata *existing = this->metadata.lookup(*commit);
  if (!existing)
    return 1;
  metadata = existing->metadata;
  return 0;
}

int git_cache::set_mono(sha1_ref split, sha1_ref mono) {
  if (commits_query(*split).insert_data(db.commits, *mono))
    return error("failed to map split " + split->to_string() + " to mono " +
                 mono->to_string());
  note_mono(split, mono);
  return 0;
}
int git_cache::compute_mono(sha1_ref split, sha1_ref &mono) {
  if (!lookup_mono(split, mono))
    return 0;

  binary_sha1 sha1;
  if (!commits_query(*split).lookup_data(db.commits, sha1)) {
    mono = pool.lookup(sha1);
    note_mono(split, mono);
    return 0;
  }

  int rev = -1;
  if (compute_rev(split, rev) || rev <= 0)
    return 1;

  auto *bytes = reinterpret_cast<const unsigned char *>(svn2git.bytes);
  long offset = 20 * rev;
  if (offset + 20 > svn2git.num_bytes)
    return 1;
  sha1.from_binary(bytes + offset);
  // TODO: add testcase for something like r137571 which has a valid SVN
  // revision but is not in the monorepo.
  mono = pool.lookup(sha1);
  if (!mono)
    return 1;
  note_mono(split, mono);
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

int git_cache::compute_metadata(sha1_ref commit, const char *&metadata) {
  if (!lookup_metadata(commit, metadata))
    return 0;

  textual_sha1 sha1(*commit);
  const char *args[] = {"git",
                        "log",
                        "--date=raw",
                        "-1",
                        "--format=format:%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B",
                        sha1.bytes,
                        nullptr};
  git_reply.clear();
  if (call_git(args, nullptr, "", git_reply))
    return error(std::string("failed to read commit metadata for ") +
                 sha1.bytes);
  if (git_reply.empty())
    return error("missing commit metadata for " + sha1.to_string());

  char *&storage = const_cast<char *&>(metadata);
  if (git_reply.size() >= 4096) {
    big_metadata.emplace_back(new char[git_reply.size() + 1]);
    storage = big_metadata.back().get();
  } else {
    storage = new (name_alloc.allocate(git_reply.size() + 1,
                                       1)) char[git_reply.size() + 1];
  }
  memcpy(storage, &git_reply[0], git_reply.size());
  storage[git_reply.size()] = 0;
  note_metadata(commit, storage);
  return 0;
}

int git_cache::set_rev(sha1_ref commit, int rev) {
  svnbaserev dbrev;
  dbrev.set_rev(rev);
  if (svnbase_query(*commit).insert_data(db.svnbase, dbrev))
    return error("failed to map commit " + commit->to_string() + " to rev " +
                 std::to_string(rev));
  note_rev(commit, rev);
  return 0;
}

int git_cache::compute_rev(sha1_ref commit, int &rev) {
  return compute_rev_with_metadata(commit, rev, nullptr);
}

int git_cache::compute_rev_with_metadata(sha1_ref commit, int &rev,
                                         const char *raw_metadata) {
  if (!lookup_rev(commit, rev))
    return 0;

  {
    svnbaserev dbrev;
    if (!svnbase_query(*commit).lookup_data(db.svnbase, dbrev)) {
      // Negative indicates it's not upstream.
      rev = -dbrev.get_rev();
      note_rev(commit, rev);
      return 0;
    }
  }

  // Manually blacklist cherry-picks that don't look like cherry-picks because
  // the committer and author metadata match exactly.  The heuristics below
  // assume that only a malicious actor could cause these to collide, but we
  // found an exception.
  //
  // FIXME: Consider whether there is a safer solution for this.
  //
  // FIXME: Is there a way to add a test for this without having the full
  // history available?
  {
    textual_sha1 text;

    // 8bf1494af87f222db2b637a3be6cee40a9a51a62 is NOT r354826.
    text.from_input("8bf1494af87f222db2b637a3be6cee40a9a51a62");
    if (binary_sha1(text) == *commit)
      return 1;
  }

  if (!raw_metadata)
    if (compute_metadata(commit, raw_metadata))
      return 1;

  parsed_metadata parsed;
  if (parse_commit_metadata_impl(raw_metadata, parsed))
    return 1;

  // Check that committer and author match.
  //
  // TODO: Add tests for all three of these heuristics.
  if (parsed.an != parsed.cn || parsed.ae != parsed.ce ||
      parsed.ad != parsed.cd)
    return 1;

  auto try_parse_string = [](const char *&current, const char *s) {
    const char *x = current;
    for (; *x && *s; ++x, ++s)
      if (*x != *s)
        return 1;
    if (*s)
      return 1;
    current = x;
    return 0;
  };
  auto parse_ch = [](const char *&current, int ch) {
    if (*current != ch)
      return 1;
    ++current;
    return 0;
  };
  auto skip_until = [](const char *&current, int ch) {
    for (; *current; ++current)
      if (*current == ch)
        return 0;
    return 1;
  };
  auto parse_num = [](const char *&current, int &num) {
    char *end = nullptr;
    long parsed_num = strtol(current, &end, 10);
    if (current == end || parsed_num > INT_MAX || parsed_num < INT_MIN)
      return 1;
    current = end;
    num = parsed_num;
    return 0;
  };

  const char *current = parsed.message;
  while (*current) {
    if (!try_parse_string(current, "llvm-rev: ")) {
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

  // FIXME: consider warning here.
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
  auto parse_token = [](const char *&current, int token) {
    if (*current != token)
      return 1;
    ++current;
    return 0;
  };
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
  auto parse_sha1 = [this](const char *&current, sha1_ref &sha1) {
    textual_sha1 text;
    const char *end = nullptr;
    if (text.from_input(current, &end))
      return 1;

    // Don't allow "0000000000000000000000000000000000000000".
    sha1 = pool.lookup(text);
    if (!sha1)
      return 1;

    current = end;
    return 0;
  };
  auto parse_name = [this](const char *&current, const char *&name) {
    const char *ch = current;
    for (; *ch; ++ch)
      if (*ch == '\n') {
        name = make_name(current, ch - current);
        current = ch;
        return 0;
      }
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

    if (parse_mode(current, last->type) || parse_token(current, ' ') ||
        parse_type(current, last->type) || parse_token(current, ' ') ||
        parse_sha1(current, last->sha1) || parse_token(current, '\t') ||
        parse_name(current, last->name) || parse_token(current, '\n'))
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
  if (parse_suffix(metadata, result.an) ||
      parse_suffix(metadata, result.cn) ||
      parse_suffix(metadata, result.ad) ||
      parse_suffix(metadata, result.cd) ||
      parse_suffix(metadata, result.ae) ||
      parse_suffix(metadata, result.ce))
    return error("failed to parse commit metadata");

  result.message = metadata;
  return 0;
}

int git_cache::parse_commit_metadata(sha1_ref commit,
                                     git_cache::commit_tree_buffers &buffers,
                                     bool is_merge) {
  const char *prefixes[] = {
      "GIT_AUTHOR_NAME=",    "GIT_COMMITTER_NAME=", "GIT_AUTHOR_DATE=",
      "GIT_COMMITTER_DATE=", "GIT_AUTHOR_EMAIL=",   "GIT_COMMITTER_EMAIL="};
  std::string *vars[] = {&buffers.an, &buffers.cn, &buffers.ad,
                         &buffers.cd, &buffers.ae, &buffers.ce};
  for (int i = 0; i < 6; ++i)
    *vars[i] = prefixes[i];

  parsed_metadata parsed;
  {
    const char *metadata;
    if (compute_metadata(commit, metadata) ||
        parse_commit_metadata_impl(metadata, parsed))
      return 1;
  }

  if (is_merge) {
    buffers.an.append("apple-llvm-mt");
    buffers.cn.append("apple-llvm-mt");
    buffers.ae.append("mt @ apple-llvm");
    buffers.ce.append("mt @ apple-llvm");
    buffers.ad.append(parsed.cd.first, parsed.cd.last);
  } else {
    buffers.an.append(parsed.an.first, parsed.an.last);
    buffers.cn.append(parsed.cn.first, parsed.cn.last);
    buffers.ae.append(parsed.ae.first, parsed.ae.last);
    buffers.ce.append(parsed.ce.first, parsed.ce.last);
    buffers.ad.append(parsed.ad.first, parsed.ad.last);
  }
  buffers.cd.append(parsed.cd.first, parsed.cd.last);

  if (!is_merge) {
    buffers.message = parsed.message;
    return 0;
  }

  // For merge commits, just extract the subject.
  const char *start = parsed.message;
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
  buffers.message = "Merge: ";
  buffers.message.append(start, body);
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
  message += "apple-llvm-split-dir: ";
  message += dir;
  if (dir[0] != '-' || dir[1])
    message += '/';
  message += '\n';
}

int git_cache::commit_tree(sha1_ref base_commit, const char *dir, sha1_ref tree,
                           const std::vector<sha1_ref> &parents,
                           sha1_ref &commit, commit_tree_buffers &buffers) {
  if (parse_commit_metadata(base_commit, buffers, /*is_merge=*/!dir))
    return error("failed to get metadata for " + base_commit->to_string());
  append_trailers(dir, base_commit, buffers.message);

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
