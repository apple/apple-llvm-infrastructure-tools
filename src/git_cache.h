// git_cache.h
#pragma once

#include "bisect_first_match.h"
#include "call_git.h"
#include "error.h"
#include "sha1_pool.h"
#include "split2monodb.h"

namespace {
struct fparent_type {
  sha1_ref commit;
  long long ct = -1;
  int index = -1;
};
struct commit_type {
  sha1_ref commit;
  sha1_ref tree;
  sha1_ref *parents = nullptr;
  int num_parents = 0;
};

struct index_range {
  int first = -1;
  unsigned count = 0;
};
struct commit_source {
  index_range commits;
  int dir_index = -1;
  bool is_root = false;
};

struct dir_mask {
  static constexpr const int max_size = 64;
  std::bitset<max_size> bits;

  bool test(int i) const { return bits.test(i); }
  void reset(int i) { bits.reset(i); }
  void set(int i, bool value = true) { bits.set(i, value); }
};

struct dir_type {
  const char *name = nullptr;
  int source_index = -1;
  sha1_ref head;

  explicit dir_type(const char *name) : name(name) {}
};
struct dir_list {
  std::vector<dir_type> list;
  dir_mask active_dirs;

  int add_dir(const char *name, bool &is_new, int &d);
  int lookup_dir(const char *name, const char *end, bool &found);
  int lookup_dir(const char *name, bool &found) {
    return lookup_dir(name, name + strlen(name), found);
  }
  void set_head(int d, sha1_ref head) {
    list[d].head = head;
    if (head)
      active_dirs.set(d);
  }
};

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
  int lookup_commit_tree(sha1_ref commit, sha1_ref &tree) const;
  int lookup_mono(sha1_ref split, sha1_ref &mono) const;
  int lookup_rev(sha1_ref commit, int &rev) const;
  int lookup_tree(git_tree &tree) const;
  int get_commit_tree(sha1_ref commit, sha1_ref &tree);
  int get_rev(sha1_ref commit, int &rev);
  int get_mono(sha1_ref split, sha1_ref &mono);
  int set_mono(sha1_ref split, sha1_ref mono);
  int ls_tree(git_tree &tree);
  int mktree(git_tree &tree);

  const char *make_name(const char *name, size_t len);
  git_tree::item_type *make_items(git_tree::item_type *first,
                                  git_tree::item_type *last);

  struct commit_tree_buffers {
    std::string cn, cd, ce;
    std::string an, ad, ae;
    std::vector<textual_sha1> parents;
    std::vector<const char *> args;
    std::string message;
  };
  int commit_tree(sha1_ref base_commit, const char *dir, sha1_ref tree,
                  const std::vector<sha1_ref> &parents, sha1_ref &commit,
                  commit_tree_buffers &buffers);

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

  sha1_trie<git_tree> trees;
  sha1_trie<sha1_pair> commit_trees;
  sha1_trie<git_svn_base_rev> revs;
  sha1_trie<sha1_pair> monos;

  std::vector<const char *> names;

  bump_allocator name_alloc;
  bump_allocator tree_item_alloc;
  split2monodb &db;
  mmapped_file &svn2git;
  sha1_pool &pool;
  dir_list &dirs;
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

int dir_list::add_dir(const char *name, bool &is_new, int &d) {
  if (!name || !*name)
    return 1;
  dir_type dir(name);
  const char *end = name;
  for (; *end; ++end) {
    if (*end >= 'a' && *end <= 'z')
      continue;
    if (*end >= 'Z' && *end <= 'Z')
      continue;
    if (*end >= '0' && *end <= '9')
      continue;
    switch (*end) {
    default:
      return 1;
    case '_':
    case '-':
    case '+':
    case '.':
      continue;
    }
  }

  bool found = false;
  d = lookup_dir(name, found);
  is_new = !found;
  if (is_new)
    list.insert(list.begin() + d, dir);
  return 0;
}
int dir_list::lookup_dir(const char *name, const char *end, bool &found) {
  found = false;
  return bisect_first_match(list.begin(), list.end(),
                            [end, name, &found](const dir_type &dir) {
                              int diff = strncmp(name, dir.name, end - name);
                              if (!diff)
                                diff = name[end - name] < 0;
                              found |= !diff;
                              return diff <= 0;
                            }) -
         list.begin();
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
  *inserted = tree;
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

int git_cache::set_mono(sha1_ref split, sha1_ref mono) {
  if (commits_query(*split).insert_data(db.commits, *mono))
    return 1;
  note_mono(split, mono);
  return 0;
}
int git_cache::get_mono(sha1_ref split, sha1_ref &mono) {
  if (!lookup_mono(split, mono))
    return 0;

  binary_sha1 sha1;
  if (!commits_query(*split).lookup_data(db.commits, sha1)) {
    mono = pool.lookup(sha1);
    note_mono(split, mono);
    return 0;
  }

  int rev = -1;
  if (get_rev(split, rev) || rev <= 0)
    return 1;

  auto *bytes = reinterpret_cast<const unsigned char *>(svn2git.bytes);
  long offset = 20 * rev;
  if (offset + 20 > svn2git.num_bytes)
    return 1;
  sha1.from_binary(bytes + offset);
  mono = pool.lookup(sha1);
  if (!mono)
    return 1;
  note_mono(split, mono);
  note_rev(mono, rev);
  return 0;
}

int git_cache::get_commit_tree(sha1_ref commit, sha1_ref &tree) {
  if (!lookup_commit_tree(commit, tree))
    return 0;

  bool once = false;
  auto reader = [&](std::string line) {
    if (once)
      return 1;
    once = true;

    textual_sha1 text;
    if (text.from_input(line.c_str()))
      return 1;

    tree = pool.lookup(text);
    note_commit_tree(commit, tree);
    return EOF;
  };

  assert(commit);
  std::string ref = textual_sha1(*commit).bytes;
  ref += "^{tree}";
  const char *argv[] = {"git", "rev-parse", "--verify", ref.c_str(), nullptr};
  return call_git(argv, nullptr, reader);
}

int git_cache::get_rev(sha1_ref commit, int &rev) {
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

  const char *llvm_rev_trailer = "llvm-rev: ";
  const int llvm_rev_trailer_len = strlen(llvm_rev_trailer);
  const char *git_svn_id_trailer =
      "git-svn-id: https://llvm.org/svn/llvm-project/";
  const int git_svn_id_trailer_len = strlen(git_svn_id_trailer);

  bool found = false;
  long parsed_rev = -1;
  std::string timestamp;
  int count = 0;
  auto reader = [&](std::string line) {
    switch (count++) {
    default:
      break;
    case 0:
      timestamp = std::move(line);
      return 0;
    case 1:
      if (line == timestamp)
        return 0;
      // Author and commit timestamps don't match.  Looks like a cherry-pick.
      return EOF;
    }
    if (found)
      return 1;

    // Check for "llvm-rev: <rev>".
    if (!line.compare(0, llvm_rev_trailer_len, llvm_rev_trailer)) {
      char *end_rev = nullptr;
      parsed_rev = strtol(line.data() + llvm_rev_trailer_len, &end_rev, 10);
      if (*end_rev)
        return 0;
      found = true;
      return EOF;
    }

    // Check for "git-svn-id: <url>@<rev> <junk>".
    if (line.compare(0, git_svn_id_trailer_len, git_svn_id_trailer))
      return 0;
    size_t at = line.find('@', git_svn_id_trailer_len);
    if (at == std::string::npos)
      return 0;

    char *end_rev = nullptr;
    parsed_rev = strtol(line.data() + at + 1, &end_rev, 10);
    if (*end_rev != ' ')
      return 0;
    found = true;
    return EOF;
  };

  textual_sha1 sha1(*commit);
  const char *argv[] = {"git", "log",      "--format=format:%at%n%ct%n%B",
                        "-1",  sha1.bytes, nullptr};
  if (call_git(argv, nullptr, reader))
    return error("failed to look up svnbaserev in git for " +
                 commit->to_string());

  if (!found) {
    // FIXME: consider warning here.
    rev = 0;
    note_rev(commit, rev);
    return 0;
  }

  if (parsed_rev > INT_MAX)
    return error("missing llvm-svn-base-rev for " + commit->to_string() +
                 " is too big");
  rev = parsed_rev;
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
    if (!*current)
      return 1;
    name = make_name(current, strlen(current));
    return 0;
  };

  constexpr const int max_items = dir_mask::max_size;
  git_tree::item_type items[max_items];
  git_tree::item_type *last = items;
  auto reader = [&](std::string line) {
    if (last - items == max_items)
      return error(
          "ls-tree: too many items (max: " + std::to_string(max_items) + ")");

    const char *current = line.c_str();
    if (parse_mode(current, last->type) || parse_token(current, ' ') ||
        parse_type(current, last->type) || parse_token(current, ' ') ||
        parse_sha1(current, last->sha1) || parse_token(current, '\t') ||
        parse_name(current, last->name))
      return error("ls-tree: could not parse entry");
    ++last;
    return EOF;
  };

  std::string ref = tree.sha1->to_string();
  const char *args[] = {"git", "ls-tree", ref.c_str(), nullptr};
  if (call_git(args, nullptr, reader))
    return 1;

  tree.num_items = last - items;
  tree.items = make_items(items, last);
  note_tree(tree);
  return 0;
}

int git_cache::mktree(git_tree &tree) {
  assert(!tree.sha1);
  bool once = false;
  auto reader = [&](std::string line) {
    if (once)
      return 1;
    once = true;

    textual_sha1 text;
    if (text.from_input(line.c_str()))
      return 1;

    tree.sha1 = pool.lookup(text);
    note_tree(tree);
    return EOF;
  };

  auto writer = [&](FILE *file, bool &stop) {
    assert(!stop);
    for (auto i = 0; i != tree.num_items; ++i) {
      assert(tree.items[i].sha1);
      if (!fprintf(file, "%s %s %s\t%s\n", tree.items[i].get_mode(),
                   tree.items[i].get_type(),
                   textual_sha1(*tree.items[i].sha1).bytes, tree.items[i].name))
        return 1;
    }
    stop = true;
    return 0;
  };

  const char *argv[] = {"git", "mktree", nullptr};
  return call_git(argv, nullptr, reader, writer);
}

static int get_commit_metadata(sha1_ref commit,
                               git_cache::commit_tree_buffers &buffers) {
  auto &message = buffers.message;
  const char *prefixes[] = {
      "GIT_AUTHOR_NAME=",    "GIT_COMMITTER_NAME=", "GIT_AUTHOR_DATE=",
      "GIT_COMMITTER_DATE=", "GIT_AUTHOR_EMAIL=",   "GIT_COMMITTER_EMAIL="};
  std::string *vars[] = {&buffers.an, &buffers.cn, &buffers.ad,
                         &buffers.cd, &buffers.ae, &buffers.ce};
  for (int i = 0; i < 6; ++i)
    *vars[i] = prefixes[i];

  message.clear();
  textual_sha1 sha1(*commit);
  const char *args[] = {"git",
                        "log",
                        "--date=raw",
                        "-1",
                        "--format=format:%an%n%cn%n%ad%n%cd%n%ae%n%ce%n%B",
                        sha1.bytes,
                        nullptr};
  size_t count = 0;
  auto reader = [&message, &count, &vars](std::string line) {
    if (count++ < 6) {
      vars[count - 1]->append(line);
      return 0;
    }

    message.append(line);
    message += '\n';
    return 0;
  };
  if (call_git(args, nullptr, reader))
    return error(std::string("failed to read commit message for ") +
                 sha1.bytes);
  if (count < 6)
    return error(std::string("missing commit metadata for ") + sha1.bytes);
  return 0;
}

static bool should_separate_trailers(const std::string &message) {
  if (message.size() < 2)
    return false;
  assert(message.end()[-1] == '\n');
  if (message.end()[-2] == '\n')
    return false;
  size_t start = message.rfind('\n', message.size() - 2);
  start = start == std::string::npos ? 0 : start + 1;
  const char *ch = message.c_str() + start;
  for (; *ch; ++ch) {
    if (*ch >= 'a' && *ch <= 'z')
      continue;
    if (*ch >= 'Z' && *ch <= 'Z')
      continue;
    if (*ch >= '0' && *ch <= '9')
      continue;
    if (*ch == '_' || *ch == '-' || *ch == '+')
      continue;
    if (*ch == ':')
      return *++ch != ' ';
    return true;
  }
  return true;
}

static void append_trailers(const char *dir, sha1_ref base_commit,
                            std::string &message) {
  if (should_separate_trailers(message))
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
  if (get_commit_metadata(base_commit, buffers))
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
  buffers.args.push_back(text_tree.bytes);
  for (auto &p : buffers.parents) {
    buffers.args.push_back("-p");
    buffers.args.push_back(p.bytes);
  }
  buffers.args.push_back(nullptr);

  bool found = false;
  auto reader = [&](std::string line) {
    if (found)
      return error("extra lines in new commit");
    found = true;
    textual_sha1 sha1;
    const char *end = nullptr;
    if (sha1.from_input(line.c_str(), &end) ||
        end != line.c_str() + line.size())
      return error("invalid sha1 for new commit");
    commit = pool.lookup(sha1);
    note_commit_tree(commit, tree);
    return 0;
  };
  auto writer = [&buffers](FILE *file, bool &stop) {
    fprintf(file, "%s", buffers.message.c_str());
    stop = true;
    return 0;
  };
  if (call_git(buffers.args.data(), envp, reader, writer))
    return 1;

  if (!found)
    return error("missing sha1 for new commit");
  return 0;
}
