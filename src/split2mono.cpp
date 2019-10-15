// split2mono.cpp
//
// Format description below:
//
// tree: split2mono
// - blob: repos (text file)
//   <header>
//   <upstream>...
//
//   <header>::   'name:' SP <name> LF
//   <upstream>:: 'upstream:' SP <name> SP <num-upstreams>
//                            SP <commits-size> SP <svnbase-size> LF
//
// - blob: commits
//   0x0000-0x0027: magic
//   0x0028-0x...: commit pairs
//   - commit: 0x28
//     0x00-0x13: split
//     0x14-0x27: mono
//
// - blob: commits.index
//   <index>
//
// - blob: svnbase
//   0x0000-0x0017: magic
//   0x0018-0x...: commit pairs
//   - commit: 0x28
//     0x00-0x13: sha1 (mono)
//     0x14-0x17: llvm svn base rev
//
// - blob: svnbase.index
//   <index>
//
//
// <index>
//   0x0000-0x0007: magic
//   0x0008-0x0807: root index bitmap (0x4000 bits)
//   0x0808-0xc807: index entries
//   0xc808-0x....: subtrie indexes
//
//   index entry: 0x3
//   bit 0x00-0x00: is-commit-pair-num? (vs subtrie-num)
//   bit 0x01-0x17: num
//
//   subtrie index: 0xc8
//   0x00-0x07: bitmap (0x40 bits)
//   0x08-0xc7: index entries
#include "call_git.h"
#include "commit_interleaver.h"
#include "error.h"
#include "file_stream.h"
#include "git_cache.h"
#include "mmapped_file.h"
#include "sha1_pool.h"
#include "sha1convert.h"
#include "split2monodb.h"
#include "svnbaserev.h"
#include <bitset>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

static int usage(const std::string &msg, const char *cmd) {
  error(msg);
  if (const char *slash = strrchr(cmd, '/'))
    cmd = slash + 1;
  fprintf(stderr,
          "usage: %s create             <dbdir> <name>\n"
          "       %s lookup             <dbdir> <split>\n"
          "       %s compute-mono       <dbdir> <svn2git-db> <split>\n"
          "       %s lookup-svnbase     <dbdir> <sha1>\n"
          "       %s upstream           <dbdir> <upstream-dbdir>\n"
          "       %s check-upstream     <dbdir> <upstream-dbdir>\n"
          "       %s insert             <dbdir> [<split> <mono>]\n"
          "       %s insert-svnbase     <dbdir> <sha1> <rev>\n"
          "       %s interleave-commits <dbdir> <svn2git-db>   \\\n"
          "                             <head> (<sha1>:<dir>)+ \\\n"
          "                                 -- (<sha1>:<dir>)+\n"
          "       %s dump               <dbdir>\n"
          "\n"
          "special handling for <sha1>:<dir> pairs\n"
          "       <dir>     '-'         root\n"
          "                 000...0     not yet started\n"
          "       <sha1>    '-'         untracked\n",
          cmd, cmd, cmd, cmd, cmd, cmd, cmd, cmd, cmd, cmd);
  return 1;
}

static int main_lookup(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("lookup: missing <dbdir>", cmd);
  if (argc < 2)
    return usage("lookup: missing <split>", cmd);
  if (argc > 2)
    return usage("lookup: too may positional args", cmd);
  const char *dbdir = argv[0];
  textual_sha1 split;
  if (split.from_input(argv[1]))
    return usage("lookup: <split> is not a valid sha1", cmd);

  split2monodb db;
  db.is_read_only = true;
  if (db.opendb(dbdir))
    return 1;

  binary_sha1 binmono;
  if (commits_query(split).lookup_data(db.commits, binmono))
    return 1;

  // TODO: add a test for the exit status.
  textual_sha1 mono(binmono);
  return printf("%s\n", mono.bytes) != 41;
}

static int main_compute_mono(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("compute-mono: missing <dbdir>", cmd);
  split2monodb db;
  if (db.opendb(argv[0]))
    return usage("could not open <dbdir>", cmd);
  --argc, ++argv;

  // Copied from svn2git.cpp.
  if (argc < 1)
    return usage("compute-mono: missing <svn2git-db>", cmd);
  mmapped_file svn2git;
  unsigned char svn2git_magic[] = {'s', 2, 'g', 0xd, 0xb, 'm', 0xa, 'p'};
  if (svn2git.init(argv[0]) ||
      svn2git.num_bytes < (long)sizeof(svn2git_magic) ||
      memcmp(svn2git_magic, svn2git.bytes, sizeof(svn2git_magic)))
    return usage("invalid <svn2git-db>", cmd);
  --argc, ++argv;

  if (argc < 1)
    return usage("compute-mono: missing <split>", cmd);
  sha1_ref split;
  sha1_pool pool;
  const char *current = argv[0];
  if (pool.parse_sha1(current, split) || *current)
    return usage("compute-mono: <split> is not a valid sha1", cmd);

  dir_list dirs;
  git_cache git(db, svn2git, pool, dirs);

  // TODO: add a test for the exit status.
  sha1_ref mono;
  if (git.compute_mono(split, mono))
    return 1;
  return printf("%s\n", mono->to_string().c_str()) != 41;
}

static int main_lookup_svnbase(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("lookup: missing <dbdir>", cmd);
  if (argc < 2)
    return usage("lookup: missing <sha1>", cmd);
  if (argc > 2)
    return usage("lookup: too may positional args", cmd);
  const char *dbdir = argv[0];
  textual_sha1 key;
  if (key.from_input(argv[1]))
    return usage("lookup: <sha1> is not a valid sha1", cmd);

  split2monodb db;
  db.is_read_only = true;
  if (db.opendb(dbdir))
    return 1;

  svnbaserev rev;
  if (svnbase_query(key).lookup_data(db.svnbase, rev))
    return 1;

  // TODO: add a test for the exit status.
  return printf("%d\n", rev.get_rev()) != 0;
}

static int main_insert_one(const char *cmd, const char *dbdir,
                           const char *rawsplit, const char *rawmono) {
  textual_sha1 split, mono;
  if (split.from_input(rawsplit))
    return usage("insert: <split> is not a valid sha1", cmd);
  if (mono.from_input(rawmono))
    return usage("insert: <mono> is not a valid sha1", cmd);

  split2monodb db;
  if (db.opendb(dbdir))
    return usage("create: failed to open <dbdir>", cmd);

  return commits_query(split).insert_data(db.commits, binary_sha1(mono));
}

static int main_insert_stdin(const char *cmd, const char *dbdir) {
  split2monodb db;
  if (db.opendb(dbdir))
    return usage("create: failed to open <dbdir>", cmd);

  char rawsplit[41] = {0};
  char rawmono[41] = {0};
  int scanned;
  while ((scanned = scanf("%40s %40s", rawsplit, rawmono)) == 2) {
    textual_sha1 split, mono;
    if (split.from_input(rawsplit))
      return error("invalid sha1 for <split>");
    if (mono.from_input(rawmono))
      return error("invliad sha1 for <mono>");
    if (commits_query(split).insert_data(db.commits, binary_sha1(mono)))
      return error("failed to insert split " + split.to_string() + " to mono " +
                   mono.to_string());
  }
  if (scanned != EOF)
    return error("sha1 for <split> could not be scanned");

  return 0;
}

static int main_insert(const char *cmd, int argc, const char *argv[]) {
  if (argc == 3)
    return main_insert_one(cmd, argv[0], argv[1], argv[2]);
  if (argc == 1)
    return main_insert_stdin(cmd, argv[0]);
  return usage("insert: wrong number of positional arguments", cmd);
}

static int main_insert_svnbase(const char *cmd, int argc, const char *argv[]) {
  if (argc != 3)
    return usage("insert: wrong number of positional arguments", cmd);

  split2monodb db;
  if (db.opendb(argv[0]))
    return 1;

  textual_sha1 key;
  if (key.from_input(argv[1]))
    return usage("insert: <sha1> is not a valid sha1", cmd);

  const char *start_rev = argv[2];
  if (start_rev[0] == 'r')
    ++start_rev;
  char *end_rev = nullptr;
  long rev = strtol(start_rev, &end_rev, 10);
  if (*end_rev || rev < 0)
    return usage("insert: <rev> is not a valid revision", cmd);

  return svnbase_query(key).insert_data(db.svnbase, svnbaserev(rev));
}

static int main_create(const char *cmd, int argc, const char *argv[]) {
  if (argc != 2)
    return usage("create: wrong number of positional arguments", cmd);
  const char *dbdir = argv[0];
  const char *name = argv[1];
  bool is_valid = *name;
  for (const char *ch = name; *ch && is_valid; ++ch) {
    if (*ch >= '0' || *ch <= '9')
      continue;
    if (*ch >= 'a' || *ch <= 'z')
      continue;
    if (*ch >= 'A' || *ch <= 'Z')
      continue;
    switch (*ch) {
    case '-':
    case '+':
    case '_':
    case '.':
      // Can't be the first or last characters.
      is_valid &= ch != name && ch[1];
      continue;

    default:
      // Not valid.
      is_valid = false;
      continue;
    }
  }
  if (!is_valid)
    return usage("create: invalid <name>; expected "
                 "[0-9a-zA-Z][0-9a-zA-Z-+._]*[0-9a-zA-Z]?",
                 cmd);

  split2monodb db;
  db.name = name;
  if (db.opendb(dbdir))
    return usage("create: failed to open <dbdir>", cmd);

  // Write out the name.
  FILE *ufile = fdopen(db.upstreamsfd, "w");
  if (!ufile)
    return error("could not reopen stream for upstreams");
  if (fprintf(ufile, "name: %s\n", db.name.c_str()) < 0)
    return error("could not write repo name");

  return 0;
}

static int main_upstream(const char *cmd, int argc, const char *argv[]) {
  if (argc != 2)
    return usage("upstream: wrong number of positional arguments", cmd);
  split2monodb main, upstream;
  upstream.is_read_only = true;
  if (main.opendb(argv[0]) || main.parse_upstreams())
    return usage("could not open <dbdir>", cmd);
  if (upstream.opendb(argv[1]) || upstream.parse_upstreams())
    return usage("could not open <dbdir>", cmd);

  // Refuse to self-reference.
  if (main.name == upstream.name)
    return error("refusing to record self as upstream");

  // Linear search for this upstream.
  auto existing_entry = main.upstreams.find(upstream.name);

  // Pretend we've already merged this upstream, but it had no commits or
  // upstreams at the time.
  bool is_new = false;
  if (existing_entry == main.upstreams.end()) {
    is_new = true;
    upstream_entry ue;
    ue.name = upstream.name;
    existing_entry = main.upstreams.emplace(upstream.name, std::move(ue)).first;
  }

  if (existing_entry->second.num_upstreams > (long)upstream.upstreams.size())
    return error("upstream is missing upstreams we already merged");

  if (existing_entry->second.commits_size > upstream.commits_size_on_open())
    return error("upstream is missing commits we already merged");

  if (existing_entry->second.svnbase_size > upstream.svnbase_size_on_open())
    return error("upstream is missing svnbase revs we already merged");

  // Nothing to do if nothing has changed (or the upstream is empty).
  if (existing_entry->second.num_upstreams == (long)upstream.upstreams.size() &&
      existing_entry->second.commits_size == upstream.commits_size_on_open() &&
      existing_entry->second.svnbase_size == upstream.svnbase_size_on_open() &&
      !is_new)
    return 0;

  // Merge the upstream's upstreams (just in memory, for now).
  for (auto ue : upstream.upstreams) {
    if (ue.second.name == main.name)
      return error("upstream: refusing to create upstream-cycle between dbs");
    auto &existing_ue = main.upstreams[ue.second.name];

    // Check that we're only moving forward.
    if (!existing_ue.name.empty())
      if (existing_ue.num_upstreams > ue.second.num_upstreams ||
          existing_ue.commits_size > ue.second.commits_size ||
          existing_ue.svnbase_size > ue.second.svnbase_size)
        return error("upstream's upstream is out-of-date");

    // Update.
    existing_ue = ue.second;
  }

  // Read all missing commits and merge them.
  if (merge_tables<commits_table>(
          main.commits, existing_entry->second.commits_size, upstream.commits,
          upstream.commits_size_on_open()) ||
      merge_tables<svnbase_table>(
          main.svnbase, existing_entry->second.svnbase_size, upstream.svnbase,
          upstream.svnbase_size_on_open()))
    return 1;

  // Close the streams.
  if (main.close_files())
    return error("error closing commits or index after writing");

  // Merge upstreams.
  existing_entry->second.num_upstreams = upstream.upstreams.size();
  existing_entry->second.commits_size = upstream.commits_size_on_open();
  existing_entry->second.svnbase_size = upstream.svnbase_size_on_open();
  int upstreamsfd = openat(main.dbfd, "upstreams", O_WRONLY | O_TRUNC);
  if (upstreamsfd == -1)
    return error("could not reopen upstreams to write merged file");
  FILE *ufile = fdopen(upstreamsfd, "w");
  if (!ufile)
    return error("could not reopen stream for upstreams");
  if (fprintf(ufile, "name: %s\n", main.name.c_str()) < 0)
    return error("could not write repo name");
  for (auto &ue : main.upstreams)
    if (fprintf(ufile,
                "upstream: %s num-upstreams=%ld commits-size=%ld "
                "svnbase-size=%ld\n",
                ue.second.name.c_str(), ue.second.num_upstreams,
                ue.second.commits_size, ue.second.svnbase_size) < 0)
      return error("could not write upstream");
  if (fclose(ufile))
    return error("problem closing new upstream");
  return 0;
}

static int main_check_upstream(const char *cmd, int argc, const char *argv[]) {
  if (argc != 2)
    return usage("upstream: wrong number of positional arguments", cmd);
  split2monodb main, upstream;
  upstream.is_read_only = main.is_read_only = true;
  if (main.opendb(argv[0]) || main.parse_upstreams())
    return usage("could not open <dbdir>", cmd);
  if (upstream.opendb(argv[1]) || upstream.parse_upstreams())
    return usage("could not open <dbdir>", cmd);

  // Refuse to self-reference.
  if (main.name == upstream.name)
    return error("refusing to check self as upstream");

  // Check if main is up-to-date.
  auto existing_entry = main.upstreams.find(upstream.name);
  if (existing_entry == main.upstreams.end() ||
      existing_entry->second.num_upstreams != (long)upstream.upstreams.size() ||
      existing_entry->second.commits_size != upstream.commits_size_on_open() ||
      existing_entry->second.svnbase_size != upstream.svnbase_size_on_open()) {
    fprintf(stderr, "'%s' is not up-to-date with '%s'\n", main.name.c_str(),
            upstream.name.c_str());
    return 1;
  }

  fprintf(stderr, "'%s' is up-to-date with '%s'\n", main.name.c_str(),
          upstream.name.c_str());
  return 0;
}

static int main_dump(const char *cmd, int argc, const char *argv[]) {
  if (argc != 1)
    return usage("dump: extra positional arguments", cmd);
  split2monodb db;
  db.is_read_only = true;
  if (db.opendb(argv[0]))
    return usage("could not open <dbdir>", cmd);

  bool has_error = false;
  has_error |= dump_table<commits_table>(db.commits);
  printf("\n");
  has_error |= dump_table<svnbase_table>(db.svnbase);
  return has_error ? 1 : 0;
}

static int main_interleave_commits(const char *cmd, int argc,
                                   const char *argv[]) {
  if (argc < 1)
    return usage("interleave-commits: missing <dbdir>", cmd);
  split2monodb db;
  if (db.opendb(argv[0]))
    return usage("could not open <dbdir>", cmd);
  --argc, ++argv;

  // Copied from svn2git.cpp.
  if (argc < 1)
    return usage("interleave-commits: missing <svn2git-db>", cmd);
  mmapped_file svn2git;
  unsigned char svn2git_magic[] = {'s', 2, 'g', 0xd, 0xb, 'm', 0xa, 'p'};
  if (svn2git.init(argv[0]) ||
      svn2git.num_bytes < (long)sizeof(svn2git_magic) ||
      memcmp(svn2git_magic, svn2git.bytes, sizeof(svn2git_magic)))
    return usage("invalid <svn2git-db>", cmd);
  --argc, ++argv;

  commit_interleaver interleaver(db, svn2git);

  if (argc < 1)
    return usage("interleave-commits: missing <head>", cmd);
  textual_sha1 head;
  if (head.from_input(*argv))
    return usage("invalid sha1 for <head>", cmd);
  interleaver.set_initial_head(head);
  --argc, ++argv;

  if (argc < 1)
    return usage("interleave-commits: missing (<ref>:<dir>)+", cmd);
  if (argc > dir_mask::max_size)
    return usage("interleave-commits: too many dirs (max: " +
                     std::to_string(dir_mask::max_size) + ")",
                 cmd);

  // Parse refs and directories.
  auto parse_sha1 = [&interleaver](const char *&current, sha1_ref &sha1) {
    // TODO: add a testcase where sha1 is non-zero.
    textual_sha1 text;
    if (text.from_input(current, &current))
      return 1;
    sha1 = interleaver.sha1s.lookup(text);
    return 0;
  };
  auto try_parse_ch = [](const char *&current, int ch) {
    if (*current != ch)
      return 1;
    ++current;
    return 0;
  };
  bool was_repeated_head_specified = false;
  for (; argc; ++argv, --argc) {
    const char *arg = *argv;
    sha1_ref head;
    bool is_tracked = false;
    bool is_repeat = false;

    // Skip over "--" and break out to continue to goals.
    if (!strcmp(arg, "--")) {
      ++argv, --argc;
      break;
    }

    if (try_parse_ch(arg, '-')) {
      is_tracked = true;
      if (!try_parse_ch(arg, '%'))
        is_repeat = true;
    }
    if ((is_tracked && !is_repeat && parse_sha1(arg, head)) ||
        try_parse_ch(arg, ':'))
      return error("invalid <sha1>:... in '" + std::string(*argv) + "'");

    if (!try_parse_ch(arg, '%')) {
      if (*arg)
        return error("invalid junk after '%' in '" + std::string(*argv) +
                     "'");

      // This is the head for all of the repeated dirs.
      if (was_repeated_head_specified)
        return error("repeated head already specified");
      was_repeated_head_specified = true;
      interleaver.repeated_head = head;
      continue;
    }

    int d = -1;
    bool is_new = false;
    if (interleaver.dirs.add_dir(arg, is_new, d))
      return error("invalid ...:<dir> in '" + std::string(*argv) + "'");
    if (!is_new)
      return usage("duplicate <dir> '" + std::string(arg) + "'", cmd);
    if (!is_tracked)
      continue;
    interleaver.dirs.tracked_dirs.set(d);
    interleaver.dirs.set_head(d, head);
    if (is_repeat) {
      interleaver.dirs.repeated_dirs.set(d);
      interleaver.dirs.list[d].is_repeated = true;
    }
  }

  if (was_repeated_head_specified && !interleaver.dirs.repeated_dirs.bits.any())
    return usage("head specified for repeated dirs, but no dirs", cmd);
  if (!was_repeated_head_specified && interleaver.dirs.repeated_dirs.bits.any())
    return usage("repeated dirs specified, but missing head", cmd);
  if (interleaver.repeated_head)
    interleaver.dirs.active_dirs.bits |= interleaver.dirs.repeated_dirs.bits;

  // Create sources now that dirs are stable.
  interleaver.initialize_sources();

  // Parse goals.
  for (; argc; ++argv, --argc) {
    const char *arg = *argv;
    sha1_ref goal;
    if (parse_sha1(arg, goal) || try_parse_ch(arg, ':'))
      return usage("invalid <sha1>:... in '" + std::string(*argv) + "'", cmd);
    if (!goal)
      return usage("invalid null goal in '" + std::string(*argv) + "'", cmd);

    if (arg[0] == '%' && !arg[1]) {
      if (!interleaver.repeat)
        return usage("goal set for undeclared repeat '%'", cmd);
      if (interleaver.repeat->goal &&
          interleaver.repeat->goal != interleaver.repeat->head)
        return usage("two goals for repeat '%'", cmd);
      interleaver.repeat->goal = goal;
      continue;
    }

    bool found = false;
    int d = interleaver.dirs.lookup_dir(arg, found);
    if (!found)
      return usage("unknown <dir> '" + std::string(arg) + "'", cmd);
    if (!interleaver.dirs.tracked_dirs.test(d))
      return usage("untracked <dir> '" + std::string(arg) + "'", cmd);
    if (interleaver.dirs.repeated_dirs.test(d))
      return usage("cannot have goal for repeat <dir> '" + std::string(arg)
                       + "'",
                   cmd);
    auto &source = interleaver.q.sources[interleaver.dirs.list[d].source_index];
    if (source.goal && source.goal != source.head)
      return usage("two goals for <dir> '" + std::string(arg) + "'", cmd);
    source.goal = goal;
  }

  for (auto &source : interleaver.q.sources)
    if (!source.goal)
      return usage(std::string("missing goal for <dir> '") +
                   (source.is_repeat
                        ? "-"
                        : interleaver.dirs.list[source.dir_index].name) +
                   "'", cmd);

  return interleaver.run();
}

int main(int argc, const char *argv[]) {
  const char *exec = argv[0];
  --argc, ++argv;
  if (argc > 0 && !strcmp(argv[0], "-C")) {
    --argc, ++argv;
    if (argc <= 0)
      return usage("missing directory with -C", exec);
    if (chdir(argv[0]))
      return usage("failed to change directory '" + std::string(argv[1]) + "'",
                   exec);
    --argc, ++argv;
  }
  if (argc <= 0)
    return usage("missing command", exec);
  const char *cmd = argv[0];
  --argc, ++argv;
#define SUB_MAIN_IMPL(STR, F)                                                  \
  do {                                                                         \
    if (!strcmp(cmd, STR))                                                     \
      return main_##F(exec, argc, argv);                                       \
  } while (false)
#define SUB_MAIN(X) SUB_MAIN_IMPL(#X, X)
#define SUB_MAIN_SVNBASE(X) SUB_MAIN_IMPL(#X "-svnbase", X##_svnbase)
  SUB_MAIN(create);
  SUB_MAIN(lookup);
  SUB_MAIN(insert);
  SUB_MAIN(upstream);
  SUB_MAIN(dump);
  SUB_MAIN_SVNBASE(lookup);
  SUB_MAIN_SVNBASE(insert);
  SUB_MAIN_IMPL("interleave-commits", interleave_commits);
  SUB_MAIN_IMPL("check-upstream", check_upstream);
  SUB_MAIN_IMPL("compute-mono", compute_mono);
#undef SUB_MAIN_IMPL
#undef SUB_MAIN
#undef SUB_MAIN_SVNBASE
  return usage("unknown command '" + std::string(cmd) + "'", exec);
}
