// svn2git.cpp
//
// Format description: sha1 corresponding to an SVN rev starts at 20*rev and
// runs for 20 bytes.
//
// - bytes 00-19: unused / file magic FIXME: add file magic
// - bytes 20-39: sha1 for commit for r1 (0s if none)
// - bytes 40-59: sha1 for commit for r2 (0s if none)
// - bytes 60-79: sha1 for commit for r3 (0s if none)
// - ...
//
// It's easy to read a revision from the command-line using xxd:
//
//     $ xxd -s $(( $REV * 20 )) -g 0 -c 20 -l 20 -p <svn2git.db 2>/dev/null ||
//       echo 0000000000000000000000000000000000000000
//
// where "0000000000000000000000000000000000000000" means the revision is not
// mapped.
//
// Or, use the svn2git 'lookup' command.
#include "mmapped_file.h"
#include "sha1convert.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>

static int show_progress(int n, int total) {
  return fprintf(stderr, "   %9d / %d commits mapped\n", n, total) < 0;
}
static int error(const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  return 1;
}
static int usage(const char *msg, const char *cmd) {
  error(msg);
  fprintf(stderr,
          "usage: %s insert <db> [<count>]\n"
          "       %s insert <db> <rev> <sha1>\n",
          cmd, cmd);
  return 1;
}

namespace {
struct svn2gitdb {
  FILE *out = nullptr;
  ~svn2gitdb() {
    if (out)
      fclose(out);
  }
};
} // end namespace

static int opendb(const char *cmd, svn2gitdb &db, const char *dbfile) {
  // TODO: add test where commits are added in two chunks.  Initial commits
  // will be deleted if we use fopen instead of open/fdopen.
  // FIXME: add file magic (check it everywhere, write it here)
  int dbfd = open(dbfile, O_WRONLY | O_CREAT);
  if (dbfd == -1)
    return usage("could not open <db> file descriptor", cmd);
  db.out = fdopen(dbfd, "wb");
  if (!db.out)
    return usage("could not open <db> stream", cmd);
  return 0;
}

static int insert_one_impl(svn2gitdb &db, int rev, const char *sha1) {
  if (rev < 1)
    return error("invalid rev < 1");
  unsigned char binsha1[21] = {0};
  sha1tobin(binsha1, sha1);
  if (fseek(db.out, rev * 20, SEEK_SET))
    return error("could not seek to rev");
  int written = fwrite(binsha1, 1, 20, db.out);
  if (!written)
    return error("no bytes written");
  if (written != 20) {
    error("not enough bytes written");
    return 2; // file is now invalid...
  }
  return 0;
}

static int cmdline_rev(const char *cmd, int &rev, const char *str) {
  if (*str == 'r')
    ++str;
  char *endstr = nullptr;
  long lrev = strtol(str, &endstr, 10);
  if (*endstr)
    return usage("<rev> is not a valid integer", cmd);
  if (lrev > std::numeric_limits<int>::max())
    return usage("<rev> is bigger than INT_MAX; probably not an SVN revision",
                 cmd);
  rev = lrev;
  return 0;
}

static int insert_one(const char *cmd, const char *dbfile, const char *revstr,
                      const char *sha1) {
  svn2gitdb db;
  int rev = 0;
  return opendb(cmd, db, dbfile) || cmdline_rev(cmd, rev, revstr) ||
         insert_one_impl(db, rev, sha1);
}

static int insert_bulk(const char *cmd, const char *dbfile,
                       const char *countstr) {
  int total = 0;
  if (countstr)
    if (sscanf(countstr, "%d", &total) != 1)
      return usage("invalid <count>", cmd);

  svn2gitdb db;
  if (opendb(cmd, db, dbfile))
    return 1;

  char sha1[41] = {0};
  int rev = 0;
  int n = 0;
  while (scanf("%d %40s", &rev, sha1) == 2) {
    if (int EC = insert_one_impl(db, rev, sha1[0] == '-' ? sha1 + 1 : sha1))
      return EC;
    if ((++n % 5000) == 0)
      if (show_progress(n, total))
        return error("could not show progress");
  }
  if (show_progress(n, total))
    return error("could not show progress");
  return 0;
}

static int main_dump(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("missing <db>", cmd);
  if (argc > 1)
    return usage("too many positional args", cmd);
  const char *dbfile = argv[0];

  mmapped_file db;
  if (db.init(dbfile))
    return error("could not read <db>");

  char sha1[41] = {0};
  auto *bytes = reinterpret_cast<const unsigned char *>(db.bytes);
  std::string spaces(8, ' ');
  long drop_space_at = 10;
  for (long offset = 20; offset < db.num_bytes; offset += 20) {
    if ((offset / 20) == drop_space_at) {
      drop_space_at *= 10;
      spaces.pop_back();
    }
    // skip all zeros.
    if (bintosha1(sha1, bytes + offset))
      continue;
    int rev = offset / 20;
    if (!printf("r%d%s %s\n", rev, spaces.c_str(), sha1))
      return 1;
  }
  return 0;
}

static int main_lookup(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("missing <db>", cmd);
  if (argc < 2)
    return usage("missing <rev>", cmd);
  if (argc > 2)
    return usage("too many positional args", cmd);
  const char *dbfile = argv[0];
  const char *revstr = argv[1];

  mmapped_file db;
  if (db.init(dbfile))
    return error("could not read <db>");

  int rev = 0;
  if (cmdline_rev(cmd, rev, revstr))
    return 1;

  char sha1[41] = {0};
  auto *bytes = reinterpret_cast<const unsigned char *>(db.bytes);
  std::string spaces(' ', 8);
  long offset = 20 * rev;
  return offset + 20 > db.num_bytes || bintosha1(sha1, bytes + offset) ||
         printf("%s\n", sha1) != 41;
}

static int main_insert(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("missing <db>", cmd);
  if (argc > 3)
    return usage("too many positional args", cmd);
  const char *db = argv[0];
  if (argc == 3)
    return insert_one(cmd, argv[0], argv[1], argv[2]);
  return insert_bulk(cmd, argv[0], argc == 1 ? nullptr : argv[1]);
}

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing command", argv[0]);
  if (!strcmp(argv[1], "dump"))
    return main_dump(argv[0], argc - 2, argv + 2);
  if (!strcmp(argv[1], "lookup"))
    return main_lookup(argv[0], argc - 2, argv + 2);
  if (!strcmp(argv[1], "insert"))
    return main_insert(argv[0], argc - 2, argv + 2);
  return usage("unknown command", argv[0]);
}
