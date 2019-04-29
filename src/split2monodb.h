// split2monodb.h
#pragma once

#include "data_query.h"
#include "error.h"
#include "file_stream.h"
#include "index_query.h"
#include <cstdio>
#include <map>

namespace {
struct upstream_entry {
  std::string name;
  long num_upstreams = -1;
  long commits_size = -1;
  long svnbase_size = -1;
};
struct split2monodb {
  bool is_verbose = false;
  bool is_read_only = false;

  bool has_read_upstreams = false;

  table_streams commits, svnbase;
  int upstreamsfd = -1;
  int dbfd = -1;
  std::string name;

  // FIXME: std::map is way overkill, we just have a few of these.
  std::map<std::string, upstream_entry> upstreams;

  split2monodb() : commits("commits"), svnbase("svnbase") {}

  int opendb(const char *dbdir);
  int parse_upstreams();
  long commits_size() const {
    return (commits.data.get_num_bytes() - commits_table::table_offset) /
           commits_table::size;
  }
  long svnbase_size() const {
    return (svnbase.data.get_num_bytes() - svnbase_table::table_offset) /
           svnbase_table::size;
  }

  int close_files();

  void log(std::string x) {
    if (!is_verbose)
      return;
    fprintf(stderr, "log: %s\n", x.c_str());
  }
};
} // end namespace

int split2monodb::close_files() {
  return commits.close_files() | svnbase.close_files();
}

int split2monodb::parse_upstreams() {
  assert(!has_read_upstreams);
  assert(upstreamsfd != -1);
  mmapped_file file(upstreamsfd);
  upstreamsfd = -1;
  if (!file.num_bytes) {
    // No upstreams.
    has_read_upstreams = true;
    return 0;
  }
  struct context {
    const char *beg;
    const char *cur;
    const char *end;
    context() = delete;
    explicit context(const mmapped_file &file)
        : beg(file.bytes), cur(file.bytes), end(file.bytes + file.num_bytes) {}
  };

  // Create some simple parsers.
  context c(file);
  auto parse_name = [&c](std::string &name) {
    auto *last = c.cur;
    for (; last != c.end; ++last) {
      // Allow "[0-9a-zA-Z./:]".
      if (*last >= '0' && *last <= '9' && last != c.cur)
        continue;
      if (*last >= 'a' && *last <= 'z')
        continue;
      if (*last >= 'A' && *last <= 'Z')
        continue;
      if (*last == '.' || *last == '/' || *last == ':')
        continue;
      break;
    }
    if (c.cur == last)
      return error("invalid name");
    if (*last != ' ')
      return error("expected space after name");
    name.assign(c.cur, last);
    c.cur = last;
    return 0;
  };
  auto parse_number = [&c](long &num) {
    auto *last = c.cur;
    if (*last == '-')
      ++last;
    for (; last != c.end; ++last) {
      if (*last >= '0' && *last <= '9')
        continue;
      break;
    }
    if (c.cur == last)
      return error("invalid number");
    std::string digits(c.cur, last);
    c.cur = last;
    num = strtol(digits.c_str(), nullptr, 10);
    return 0;
  };
  auto parse_space = [&c](bool needs_any, bool newlines) {
    // Pull out a separate flag for needing newlines, for simplicity.
    bool needs_newline = newlines && needs_any;
    while (*c.cur == ' ' || *c.cur == '\t' || *c.cur == '\n') {
      if (*c.cur == '\n') {
        if (!newlines)
          return error("unexpected newline");
        needs_newline = false;
      }
      ++c.cur;
      needs_any = false;
    }
    if (needs_newline)
      return error("missing newline");
    if (needs_any)
      return error("expected space");
    return 0;
  };
  auto parse_label = [&c](const char *label) {
    while (c.cur != c.end && *label) {
      if (*c.cur == *label)
        continue;
      return 1;
    }
    return *label ? 1 : 0;
  };

  // Parse.
  if (parse_space(/*needs_any=*/false, /*newlines=*/true) ||
      parse_label("name:") ||
      parse_space(/*needs_any=*/true, /*newlines=*/false) || parse_name(name) ||
      parse_space(/*needs_any=*/true, /*newlines=*/true))
    return error("could not parse name:");
  while (c.cur != c.end) {
    upstream_entry ue;
    if (parse_label("upstream:") ||
        parse_space(/*needs_any=*/true, /*newlines=*/false) ||
        parse_name(ue.name) ||
        parse_space(/*needs_any=*/true, /*newlines=*/false) ||
        parse_number(ue.num_upstreams) ||
        parse_space(/*needs_any=*/true, /*newlines=*/false) ||
        parse_number(ue.commits_size) ||
        parse_space(/*needs_any=*/true, /*newlines=*/false) ||
        parse_number(ue.svnbase_size) ||
        parse_space(/*needs_any=*/true, /*newlines=*/true))
      return 1;
    if (ue.name == name)
      return error("upstream has same name as main repo");
    std::string copy_name = ue.name;
    if (!upstreams.emplace(std::move(copy_name), std::move(ue)).second)
      return error("duplicate upstream");
  }
  has_read_upstreams = true;
  return 0;
}

int split2monodb::opendb(const char *dbdir) {
  const unsigned char commits_magic[] = {'s', 2, 'm', 0xc, 0x0, 'm', 't', 's'};
  const unsigned char svnbase_magic[] = {'s', 2, 'm', 0xb, 0xa, 0x5, 0xe, 'r'};
  assert(sizeof(commits_magic) == magic_size);
  assert(sizeof(svnbase_magic) == magic_size);

  if (const char *verbose = getenv("VERBOSE"))
    if (strcmp(verbose, "0"))
      is_verbose = true;
  auto &db = *this;
  dbfd = open(dbdir, O_RDONLY);
  if (dbfd == -1)
    return error("could not open <dbdir>");

  int flags = db.is_read_only ? O_RDONLY : (O_RDWR | O_CREAT);
  if (db.commits.init(dbfd, db.is_read_only, commits_magic,
                      commits_table::table_offset, commits_table::size) ||
      db.svnbase.init(dbfd, db.is_read_only, svnbase_magic,
                      svnbase_table::table_offset, svnbase_table::size))
    return 1;

  int upstreamsfd = openat(dbfd, "upstreams", flags);
  if (upstreamsfd == -1)
    return db.is_read_only ? 1 : error("could not open <dbdir>/upstreams");
  if (!db.is_read_only)
    fchmod(upstreamsfd, 0644);

  this->upstreamsfd = upstreamsfd;
  return 0;
}

template <class T>
static int merge_tables(table_streams &main, size_t recorded_size,
                        table_streams &upstream, size_t actual_size) {
  typedef T table_type;
  typedef typename table_type::value_type value_type;

  // Read all missing commits and merge them.
  long first_offset =
      table_type::table_offset + table_type::size * recorded_size;
  long num_bytes = table_type::size * (actual_size - recorded_size);
  if (!num_bytes)
    return 0;

  // FIXME: This copy is unfortunate.  We should be reading directly from the
  // mmapped upstream db.
  std::vector<unsigned char> bytes;
  bytes.resize(num_bytes, 0);
  if (upstream.data.seek_and_read(first_offset, bytes.data(), num_bytes) !=
      num_bytes)
    return error("could not read new data from upstream");

  for (const unsigned char *b = bytes.data(), *be = bytes.data() + bytes.size();
       b != be; b += table_type::size)
    if (data_query<T>::from_binary(b).insert_data(
            main, value_type::make_from_binary(b + 20)))
      return error("error inserting new data from upstream");
  return 0;
}
