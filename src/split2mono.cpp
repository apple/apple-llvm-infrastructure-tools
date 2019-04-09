// split2mono.cpp
//
// build: clang -x c++ -std=gnu++17 -O2 -lsqlite3
// alias: split2mono-insert
#include <cstdio>
#include <cstring>
#include <fcntl.h>

constexpr int SQLITE_OPEN_READONLY = 1;
constexpr int SQLITE_OPEN_READWRITE = 2;
constexpr int SQLITE_OPEN_CREATE = 4;

extern "C" {
typedef void *sqlite3_handle;
int sqlite3_open_v2(const char *filename, sqlite3_handle *db, int flags,
                    const char *vfs);
int sqlite3_close_v2(sqlite3_handle db);
void sqlite3_free(void *mem);
int sqlite3_exec(sqlite3_handle db, const char *sql,
                 int (*callback)(void *, int, char **, char **), void *context,
                 char **error);
} // extern "C"

int show_progress(int total) {
  return fprintf(stderr, "   %9d commits mapped\n", total) < 0;
}
int error(const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  return 1;
}

int execute(sqlite3_handle db, const char *command) {
  char *error;
  if (!sqlite3_exec(db, command, nullptr, nullptr, &error))
    return 0;
  sqlite3_free(error);
  return 1;
}

int create_tables(sqlite3_handle db, const char *name) {
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

int initialize_db_readonly(sqlite3_handle *db, const char *filename) {
  if (!sqlite3_open_v2(filename, db, SQLITE_OPEN_READONLY, nullptr))
    return 0;
  sqlite3_close_v2(*db);
  *db = 0;
  return 1;
}

int initialize_db(sqlite3_handle *db, const char *name, const char *filename,
                  int(*usage)(const char *, int, const char *[]),
                  int argc, const char *argv[]) {
  if (!sqlite3_open_v2(filename, db, SQLITE_OPEN_READWRITE, nullptr))
    return 0;
  sqlite3_close_v2(*db);

  if (sqlite3_open_v2(filename, db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr))
    return usage("can't open <db>", argc, argv);
  if (!create_tables(*db, name))
    return 0;

  sqlite3_close_v2(*db);
  *db = nullptr;
  if (remove(filename))
    return error("can't remove <db> (after creating it!)");
  return 1;
}

int insert_commits(sqlite3_handle db) {
  const char prefix[] = "INSERT INTO split2mono(split,mono)VALUES(\"";
  const char record_delim[] = "\"),(\"";
  const char value_delim[] = "\",\"";
  const char suffix[] = "\");";
  char buffer[2048] = {0};
  const int record_size = 80 + sizeof(value_delim) + sizeof(record_delim);
  const int max_records =
      (sizeof(buffer) - 1 - sizeof(prefix) - sizeof(suffix)) /
      record_size;
  char *record_start =
      buffer + snprintf(buffer, sizeof(prefix), "%s", prefix);
  char *current = record_start;

  char split[41];
  char mono[41];
  int n = 0;
  int progress = 0;
  while (scanf("%s %s", split, mono) == 2) {
    current += snprintf(current, record_size, "%s%s%s", split, value_delim,
                        mono);

    if (n < max_records) {
      current += snprintf(current, sizeof(record_delim), "%s", record_delim);
      n = 0;
      continue;
    }

    current += snprintf(current, sizeof(suffix), "%s", suffix);
    *current = '\0';
    if (execute(db, buffer))
      return 1;
    current = record_start;

    if (n - progress > 5000) {
      progress = n;
      if (show_progress(n))
        return 1;
    }
  }
  if (!n)
    return 0;
  current += snprintf(current, sizeof(suffix), "%s", suffix);
  *current = '\0';
  return execute(db, buffer);
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
int split2mono_insert(sqlite3_handle *db, int argc, const char *argv[]) {
  if (argc < 3)
    return insert_usage("missing positional arguments", argc, argv);
  const char *name = argv[1];
  const char *filename = argv[2];

  if (initialize_db(db, name, filename, insert_usage, argc, argv))
    return 1;
  return insert_commits(*db);
}

int merge_upstream(sqlite3_handle db, sqlite3_handle udb) {
  (void)db;
  (void)udb;
  return error("split2mono-upstream is not yet implemented");
}
int split2mono_upstream(sqlite3_handle *db, int argc, const char *argv[]) {
  if (argc < 4)
    return upstream_usage("missing positional arguments", argc, argv);
  const char *name = argv[1];
  const char *filename = argv[2];
  const char *upstream = argv[3];

  if (initialize_db(db, name, filename, upstream_usage, argc, argv))
    return 1;

  sqlite3_handle udb = nullptr;
  if (initialize_db_readonly(&udb, upstream))
    return error("could not open <upstream>");

  return merge_upstream(*db, udb);
}

int main(int argc, const char *argv[]) {
  sqlite3_handle db = nullptr;
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
