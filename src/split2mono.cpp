// split2mono.cpp
//
// build: clang -x c++ -std=gnu++17 -O2 -lsqlite3 -lc++
// alias: split2mono-insert
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>

constexpr int SQLITE_OPEN_READONLY = 1;
constexpr int SQLITE_OPEN_READWRITE = 2;
constexpr int SQLITE_OPEN_CREATE = 4;

extern "C" {
struct sqlite3;
struct sqlite3_stmt;
int sqlite3_open_v2(const char *filename, sqlite3 **db, int flags,
                    const char *vfs);
int sqlite3_close_v2(sqlite3 *db);
void sqlite3_free(void *mem);
int sqlite3_exec(sqlite3 *db, const char *sql,
                 int (*callback)(void *, int, char **, char **), void *context,
                 char **error);
int sqlite3_bind_text(sqlite3_stmt *, int, const char *, int, void (*)(void *));
int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int numbytes,
                       sqlite3_stmt **stmt, const char **tail);
} // extern "C"

int show_progress(int total) {
  return fprintf(stderr, "   %9d commits mapped\n", total) < 0;
}
int error(const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  return 1;
}

/// Check that the name is valid.
///
/// Ideally we can restrict this enough we never need to escape it (or drop
/// down to sqlite3_bind_text).
static bool is_name_valid(const char *name) {
  for (; *name; ++name) {
    // Allow "[0-9a-zA-Z./:]".
    if (*name >= '0' && *name <= '9')
      continue;
    if (*name >= 'a' && *name <= 'z')
      continue;
    if (*name >= 'A' && *name <= 'Z')
      continue;
    if (*name == '.' || *name == '/' || *name == ':')
      continue;
    return false;
  }
  return true;
}
static bool is_name_valid(const std::string &name) {
  return is_name_valid(name.c_str());
}
static bool is_commit_valid(const char *sha1) {
  for (; *sha1; ++sha1) {
    // Allow "[0-9a-z]".
    if (*sha1 >= '0' && *sha1 <= '9')
      continue;
    if (*sha1 >= 'a' && *sha1 <= 'z')
      continue;
    return false;
  }
  return true;
}
static bool is_commit_valid(const std::string &sha1) {
  return is_commit_valid(sha1.c_str());
}

template <class T> struct CallbackContext {
  T callable;
  static int callback(void *context, int numcols, char **cols, char **names) {
    return (*reinterpret_cast<T *>(context))(numcols, cols, names);
  }
};

template <class T> int execute(sqlite3 *db, const char *command, T callback) {
  CallbackContext<T> context = {callback};
  return sqlite3_exec(db, command, CallbackContext<T>::callback, &context,
                      nullptr);
}
int execute(sqlite3 *db, const char *command) {
  return sqlite3_exec(db, command, nullptr, nullptr, nullptr);
}

int extract_string(std::string &s, sqlite3 *db, const char *query) {
  bool set = false;
  return execute(db, query,
                 [&s, &set](int numcols, char **cols, char **) {
                   if (numcols != 1)
                     return 1;
                   if (set)
                     return 1;
                   set = true;
                   s = cols[0];
                   return 0;
                 }) &&
         set;
}

int extract_long(long &l, sqlite3 *db, const char *query) {
  bool set = false;
  return execute(db, query,
                 [&l, &set](int numcols, char **cols, char **) {
                   if (numcols != 1)
                     return 1;
                   if (set)
                     return 1;
                   set = true;
                   char *endptr = nullptr;
                   l = strtol(cols[0], &endptr, 10);
                   return *endptr ? 1 : 0;
                 }) &&
         set;
}

int check_name(sqlite3 *db, const char *name) {
  if (!is_name_valid(name))
    return error("invalid name provided");
  std::string stored_name;
  if (extract_string(stored_name, db, "SELECT name FROM nameis"))
    return error("could not extract stored name");
  if (!is_name_valid(stored_name))
    return error("stored name is invalid");
  if (name != stored_name)
    return error("mismatch between db and name");
  return 0;
}

int create_tables(sqlite3 *db, const char *name) {
  if (!execute(db, "CREATE TABLE mynameis (name TEXT UNIQUE NOT NULL);"))
    return error("could not create 'mynameis' table");
  char mynameis[1024] = {0};
  int n = snprintf(mynameis, sizeof(mynameis),
                   "INSERT INTO mynameis VALUES(\"%s\");", name);
  if (n == sizeof(mynameis))
    return error("<name> is too long");
  if (!execute(db, mynameis))
    return error("could not insert name into 'mynameis' table");

  if (!execute(db, "CREATE TABLE upstream_dbs ("
                   "id INT PRIMARY KEY NOT NULL,"
                   "name TEXT UNIQUE NOT NULL,"
                   "upstream_dbs_count INT NOT NULL,"
                   "split2mono_count INT NOT NULL);"))
    return error("could not create 'upstreams' table");
  if (!execute(db, "CREATE TABLE split2mono ("
                   "id INT PRIMARY KEY NOT NULL,"
                   "split CHAR(40) UNIQUE NOT NULL,"
                   "mono CHAR(40) NOT NULL);"))
    return error("could not create 'split2mono' table");
  return 0;
}

int initialize_db_readonly(sqlite3 **db, const char *filename) {
  if (!sqlite3_open_v2(filename, db, SQLITE_OPEN_READONLY, nullptr))
    return 0;
  sqlite3_close_v2(*db);
  *db = 0;
  return 1;
}

int initialize_db(sqlite3 **db, const char *name, const char *filename,
                  int (*usage)(const char *, int, const char *[]), int argc,
                  const char *argv[]) {
  if (!sqlite3_open_v2(filename, db, SQLITE_OPEN_READWRITE, nullptr))
    return check_name(*db, name);
  sqlite3_close_v2(*db);

  if (sqlite3_open_v2(filename, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr))
    return usage("can't open <db>", argc, argv);
  if (!create_tables(*db, name))
    return 0;

  sqlite3_close_v2(*db);
  *db = nullptr;
  if (remove(filename))
    return error("can't remove <db> (after creating it!)");
  return 1;
}

int insert_commits(sqlite3 *db) {
  constexpr const char prefix[] = "INSERT INTO split2mono(split,mono)VALUES(\"";
  constexpr const char record_delim[] = "\"),(\"";
  constexpr const char value_delim[] = "\",\"";
  constexpr const char suffix[] = "\");";

  // Do 20 records at a time.
  constexpr const int max_records = 20;

  std::string query = prefix;
  char split[41];
  char mono[41];
  int n = 0;
  int progress = 0;
  while (scanf("%40s %40s", split, mono) == 2) {
    if (!is_commit_valid(split))
      return error("invalid split commit");
    if (!is_commit_valid(mono))
      return error("invalid mono commit");
    query += split;
    query += value_delim;
    query += mono;

    ++n;
    if (n < max_records) {
      query += record_delim;
      continue;
    }

    query += suffix;
    if (execute(db, query.c_str()))
      return 1;
    query.erase(sizeof(prefix));

    if (n - progress > 5000) {
      progress = n;
      if (show_progress(n))
        return 1;
    }
  }
  if (n == progress)
    return 0;
  query += suffix;
  if (execute(db, query.c_str()))
    return 1;
  return show_progress(n);
}

int insert_usage(const char *msg, int argc, const char *argv[]) {
  error(msg);
  fprintf(stderr, "usage: %s <db-name> <db>\n", argv[0]);
  return 1;
}
int upstream_usage(const char *msg, int argc, const char *argv[]) {
  error(msg);
  fprintf(stderr, "usage: %s <db-name> <db> <upstream-db>\n", argv[0]);
  return 1;
}
int split2mono_insert(sqlite3 **db, int argc, const char *argv[]) {
  if (argc < 3)
    return insert_usage("missing positional arguments", argc, argv);
  const char *name = argv[1];
  const char *filename = argv[2];

  if (initialize_db(db, name, filename, insert_usage, argc, argv))
    return 1;
  return insert_commits(*db);
}

struct UpstreamInfo {
  std::string name;
  long upstream_dbs_count = -1;
  long split2mono_count = -1;
};

int compute_upstream_info(sqlite3 *udb, UpstreamInfo &computed) {
  assert(computed.name.empty());
  if (extract_string(computed.name, udb, "SELECT name FROM nameis") ||
      extract_long(computed.upstream_dbs_count, udb,
                   "SELECT COUNT(*) FROM upstream_dbs") ||
      extract_long(computed.split2mono_count, udb,
                   "SELECT COUNT(*) FROM split2mono"))
    return error("could not get upstream's computed info");

  if (!is_name_valid(computed.name))
    return error("upstream's name looks invalid");
  return 0;
}

int extract_stored_upstream_info(sqlite3 *db, UpstreamInfo &stored,
                                 const UpstreamInfo &computed) {
  assert(!computed.name.empty());
  assert(stored.name.empty());
  std::string query = "SELECT upstream_dbs_count,split2mono_count"
                      " FROM upstream_dbs"
                      " WHERE name=\"" +
                      computed.name + "\"";

  if (!execute(db, query.c_str(),
               [&computed, &stored](int numcols, char **cols, char **) {
                 if (numcols != 2)
                   return 1;
                 if (!stored.name.empty())
                   return 1;
                 stored.name = computed.name;
                 char *endptr = nullptr;
                 stored.upstream_dbs_count = strtol(cols[0], &endptr, 10);
                 if (*endptr)
                   return 1;
                 endptr = nullptr;
                 stored.split2mono_count = strtol(cols[1], &endptr, 10);
                 return *endptr ? 1 : 0;
               }))
    return error("could not get upstream name");

  if (computed.upstream_dbs_count < stored.upstream_dbs_count ||
      computed.split2mono_count < stored.split2mono_count)
    return error("upstream db is out-of-date");

  return 0;
}

static int merge_split2mono(sqlite3 *db, const UpstreamInfo &stored,
                            sqlite3 *udb, const UpstreamInfo &computed) {
  assert(stored.split2mono_count < computed.split2mono_count);
  // Add commits.
  //
  // - open a pipe
  // - spawn a thread that reads the pipe, running insert_commits
  // - read commits
  // - write them

  return error("split2mono-upstream is not yet implemented");
}

static int merge_upstream_dbs(sqlite3 *db, const UpstreamInfo &stored,
                              sqlite3 *udb, const UpstreamInfo &computed) {
  assert(stored.upstream_dbs_count < computed.upstream_dbs_count);
  // Add upstreams.
  //
  // - no reason for a thread here, there won't be many upstreams to
  // incorporate.

  return error("split2mono-upstream is not yet implemented");
}
int merge_upstream(sqlite3 *db, sqlite3 *udb) {
  UpstreamInfo computed, stored;
  if (compute_upstream_info(udb, computed) ||
      extract_stored_upstream_info(db, stored, computed))
    return 1;
  assert(computed.name == stored.name);
  assert(computed.split2mono_count >= stored.split2mono_count);
  assert(computed.upstream_dbs_count >= stored.upstream_dbs_count);

  // Merge split2mono first.
  if (computed.split2mono_count != stored.split2mono_count)
    if (merge_split2mono(db, stored, udb, computed))
      return 1;

  if (computed.upstream_dbs_count != stored.upstream_dbs_count)
    if (merge_upstream_dbs(db, stored, udb, computed))
      return 1;

  return 0;
}
int split2mono_upstream(sqlite3 **db, int argc, const char *argv[]) {
  if (argc < 4)
    return upstream_usage("missing positional arguments", argc, argv);
  const char *name = argv[1];
  const char *filename = argv[2];
  const char *upstream = argv[3];

  if (initialize_db(db, name, filename, upstream_usage, argc, argv))
    return 1;

  sqlite3 *udb = nullptr;
  if (initialize_db_readonly(&udb, upstream))
    return error("could not open <upstream>");

  return merge_upstream(*db, udb);
}

int main(int argc, const char *argv[]) {
  sqlite3 *db = nullptr;
  const char *name = argv[0];
  if (const char *slash = strrchr(name, '/'))
    name = slash + 1;
  int status;
  if (strcmp(name, "split2mono-insert")) {
    status = split2mono_insert(&db, argc, argv);
  } else if (strcmp(name, "split2mono-upstream")) {
    status = split2mono_upstream(&db, argc, argv);
  } else {
    fprintf(stderr, "unrecognized invocation name '%s'", name);
    return 1;
  }
  if (db)
    sqlite3_close_v2(db);
  return status;
}
