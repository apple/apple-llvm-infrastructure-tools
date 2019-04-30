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
#include <vector>

static int usage(const std::string &msg, const char *cmd) {
  error(msg);
  if (const char *slash = strrchr(cmd, '/'))
    cmd = slash + 1;
  fprintf(stderr,
          "usage: %s create             <dbdir>\n"
          "       %s lookup             <dbdir> <split>\n"
          "       %s lookup-svnbase     <dbdir> <sha1>\n"
          "       %s upstream           <dbdir> <upstream-dbdir>\n"
          "       %s insert             <dbdir> [<split> <mono>]\n"
          "       %s insert-svnbase     <dbdir> <sha1> <rev>\n"
          "       %s interleave-commits <dbdir> <svn2git-db>\n"
          "                                     <head> (<sha1>:<dir>)+]\n"
          "       %s dump               <dbdir>\n"
          "\n"
          "   <dbdir>/upstreams: merged upstreams (text)\n"
          "   <dbdir>/commits: translated commits (bin)\n"
          "   <dbdir>/index: translated commits (bin)\n",
          cmd, cmd, cmd, cmd, cmd, cmd, cmd, cmd);
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
      return 1;
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
  if (argc != 1)
    return usage("create: wrong number of positional arguments", cmd);
  split2monodb db;
  if (db.opendb(argv[0]))
    return usage("create: failed to open <dbdir>", cmd);
  return 0;
}

static int main_upstream(const char *cmd, int argc, const char *argv[]) {
  if (argc != 2)
    return usage("upstream: wrong number of positional arguments", cmd);
  split2monodb main, upstream;
  upstream.is_read_only = true;
  if (main.opendb(argv[0]) || main.parse_upstreams())
    return usage("could not open <dbdir>", cmd);
  if (upstream.opendb(argv[0]) || upstream.parse_upstreams())
    return usage("could not open <dbdir>", cmd);

  // Linear search for this upstream.
  auto existing_entry = main.upstreams.find(upstream.name);

  // Pretend we've already merged this upstream, but it had no commits or
  // upstreams at the time.
  if (existing_entry == main.upstreams.end()) {
    upstream_entry ue;
    ue.name = upstream.name;
    ue.commits_size = ue.num_upstreams = 0;
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
      existing_entry->second.svnbase_size == upstream.svnbase_size_on_open())
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
  existing_entry->second.commits_size = upstream.commits_size_on_open();
  existing_entry->second.num_upstreams = upstream.upstreams.size();
  int upstreamsfd = openat(main.dbfd, "upstreams", O_WRONLY | O_TRUNC);
  if (upstreamsfd == -1)
    return error("could not reopen upstreams to write merged file");
  FILE *ufile = fdopen(upstreamsfd, "w");
  if (!ufile)
    return error("could not reopen stream for upstreams");
  if (fprintf(ufile, "name: %s\n", main.name.c_str()) < 0)
    return error("could not write repo name");
  for (auto &ue : main.upstreams)
    if (fprintf(ufile, "upstream: %s %ld %ld %ld\n", ue.second.name.c_str(),
                ue.second.num_upstreams, ue.second.commits_size,
                ue.second.svnbase_size) < 0)
      return error("could not write upstream");
  if (fclose(ufile))
    return error("problem closing new upstream");
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
  interleaver.set_head(head);
  --argc, ++argv;

  if (argc < 1)
    return usage("interleave-commits: missing (<ref>:<dir>)+", cmd);
  if (argc > dir_mask::max_size)
    return usage("interleave-commits: too many dirs (max: " +
                     std::to_string(dir_mask::max_size) + ")",
                 cmd);

  // Parse refs and directories.
  auto parse_sha1 = [&interleaver](const char *&current, sha1_ref sha1) {
    textual_sha1 text;
    if (text.from_input(current, &current))
      return 1;
    sha1 = interleaver.sha1s.lookup(text);
    return 0;
  };
  bool is_new = false;
  for (int i = 0; i < argc; ++i) {
    const char *arg = argv[i];
    sha1_ref head;
    int d = -1;
    if (parse_sha1(arg, head) || *arg++ != ':' ||
        interleaver.dirs.add_dir(arg, is_new, d))
      return error("invalid <sha1>:<dir> '" + std::string(argv[i]) + "'");
    if (!is_new)
      return usage("duplicate <dir> '" + std::string(arg) + "'", cmd);
    interleaver.dirs.set_head(d, head);
    interleaver.has_root |= !strcmp("-", arg);
  }

  return interleaver.read_queue_from_stdin() || interleaver.interleave();
}

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing command", argv[0]);
#define SUB_MAIN_IMPL(STR, F)                                                  \
  do {                                                                         \
    if (!strcmp(argv[1], STR))                                                 \
      return main_##F(argv[0], argc - 2, argv + 2);                            \
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
#undef SUB_MAIN_IMPL
#undef SUB_MAIN
#undef SUB_MAIN_SVNBASE
  return usage("unknown command '" + std::string(argv[1]) + "'", argv[0]);
}
