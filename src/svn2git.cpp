// svn2git.cpp
//
// Format description: sha1 corresponding to an SVN rev starts at 20*rev and
// runs for 20 bytes.
//
// - bytes 00-19: header
//         00-07: magic
//         08-11: version
//         12-19: unused
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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>

constexpr const unsigned version = 0;
constexpr const unsigned char magic[] = {'s', 2, 'g', 0xd, 0xb, 'm', 0xa, 'p'};
static_assert(sizeof(magic) == 8);

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
          "       %s insert <db> <rev> <sha1>\n"
          "       %s lookup <db> <rev>\n"
          "       %s create <db>\n"
          "       %s dump   <db>\n",
          cmd, cmd, cmd, cmd, cmd);
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

static void build_magic_and_version(unsigned char *bytes) {
  // Magic.
  memcpy(bytes, magic, 8);

  // Version.
  unsigned long long long_version = version;
  for (int i = 0; i < 4; ++i)
    bytes[8 + i] = (long_version << 8) >> (i * 8 + 8);
}

static int check_magic_and_version(const unsigned char *bytes) {
  unsigned char expected[20] = {0};
  build_magic_and_version(expected);
  return memcmp(expected, bytes, 20);
}

static int check_db(const unsigned char *bytes, size_t num_bytes_total) {
  if (num_bytes_total < 20 || check_magic_and_version(bytes))
    return error("<db> has bad magic or version");
  if (num_bytes_total % 20)
    return error("<db> has incomplete entries");
  return 0;
}

static int opendb(const char *cmd, svn2gitdb &db, const char *dbfile,
                  bool only_create) {
  int dbfd = open(dbfile, O_RDWR | O_CREAT | (only_create ? O_EXCL : 0));
  if (dbfd == -1) {
    if (errno == EEXIST)
      return error("cannot create already-existing <db>");
    return usage("could not open <db> file descriptor", cmd);
  }
  fchmod(dbfd, 0644);
  db.out = fdopen(dbfd, "w+b");
  if (!db.out)
    return usage("could not open <db> stream", cmd);

  if (fseek(db.out, 0, SEEK_END))
    return error("could not compute size of <db>");
  long num_bytes = ftell(db.out);

  unsigned char bytes[20] = {0};
  if (num_bytes) {
    if (fseek(db.out, 0, SEEK_SET) || fread(bytes, 1, 20, db.out) != 20)
      return error("could not read svn2git magic and version");
    return check_db(bytes, num_bytes);
  }

  build_magic_and_version(bytes);
  if (fseek(db.out, 0, SEEK_SET) || fwrite(bytes, 1, 20, db.out) != 20)
    return error("could not write svn2git magic and version");
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
  if (lrev < 1)
    return usage("<rev> must be at least 1; r0 does not exist", cmd);
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
  return opendb(cmd, db, dbfile, /*only_create=*/false) ||
         cmdline_rev(cmd, rev, revstr) || insert_one_impl(db, rev, sha1);
}

static int insert_bulk(const char *cmd, const char *dbfile,
                       const char *countstr) {
  int total = 0;
  if (countstr)
    if (sscanf(countstr, "%d", &total) != 1)
      return usage("insert: invalid <count>", cmd);

  svn2gitdb db;
  if (opendb(cmd, db, dbfile, /*only_create=*/false))
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
    return usage("dump: missing <db>", cmd);
  if (argc > 1)
    return usage("dump: too many positional args", cmd);
  const char *dbfile = argv[0];

  mmapped_file db;
  if (db.init(dbfile))
    return error("could not read <db>");
  auto *bytes = reinterpret_cast<const unsigned char *>(db.bytes);
  if (check_db(bytes, db.num_bytes))
    return 1;

  char sha1[41] = {0};
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
    return usage("lookup: missing <db>", cmd);
  if (argc < 2)
    return usage("lookup: missing <rev>", cmd);
  if (argc > 2)
    return usage("lookup: too many positional args", cmd);
  const char *dbfile = argv[0];
  const char *revstr = argv[1];

  mmapped_file db;
  if (db.init(dbfile))
    return error("could not read <db>");
  auto *bytes = reinterpret_cast<const unsigned char *>(db.bytes);
  if (check_db(bytes, db.num_bytes))
    return 1;

  int rev = 0;
  if (cmdline_rev(cmd, rev, revstr))
    return 1;

  char sha1[41] = {0};
  std::string spaces(' ', 8);
  long offset = 20 * rev;
  return offset + 20 > db.num_bytes || bintosha1(sha1, bytes + offset) ||
         printf("%s\n", sha1) != 41;
}

static int main_insert(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("insert: missing <db>", cmd);
  if (argc > 3)
    return usage("insert: too many positional args", cmd);
  const char *db = argv[0];
  if (argc == 3)
    return insert_one(cmd, argv[0], argv[1], argv[2]);
  return insert_bulk(cmd, argv[0], argc == 1 ? nullptr : argv[1]);
}

static int main_create(const char *cmd, int argc, const char *argv[]) {
  if (argc < 1)
    return usage("create: missing <db>", cmd);
  if (argc > 1)
    return usage("create: too many positional args", cmd);
  const char *dbfile = argv[0];

  svn2gitdb db;
  return opendb(cmd, db, dbfile, /*only_create=*/true);
}

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing command", argv[0]);
#define SUBMAIN(X)                                                             \
  do {                                                                         \
    if (!strcmp(argv[1], #X))                                                  \
      return main_##X(argv[0], argc - 2, argv + 2);                            \
  } while (false)
  SUBMAIN(dump);
  SUBMAIN(lookup);
  SUBMAIN(insert);
  SUBMAIN(create);
#undef SUBMAIN
  return usage("unknown command", argv[0]);
}
