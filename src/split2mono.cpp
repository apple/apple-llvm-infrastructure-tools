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
#include "mmapped_file.h"
#include "sha1convert.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <spawn.h>
#include <string>
#include <vector>

static int show_progress(int n, int total) {
  return fprintf(stderr, "   %9d / %d commits mapped\n", n, total) < 0;
}
static int error(const std::string &msg) {
  fprintf(stderr, "error: %s\n", msg.c_str());
  return 1;
}

static int usage(const char *msg, const char *cmd) {
  error(msg);
  if (const char *slash = strrchr(cmd, '/'))
    cmd = slash + 1;
  fprintf(stderr,
          "usage: %s create         <dbdir>\n"
          "       %s lookup         <dbdir> <split>\n"
          "       %s lookup-svnbase <dbdir> <sha1>\n"
          "       %s upstream       <dbdir> <upstream-dbdir>\n"
          "       %s insert         <dbdir> [<split> <mono>]\n"
          "       %s insert-svnbase <dbdir> <sha1> <rev>\n"
          "       %s dump           <dbdir>\n"
          "\n"
          "   <dbdir>/upstreams: merged upstreams (text)\n"
          "   <dbdir>/commits: translated commits (bin)\n"
          "   <dbdir>/index: translated commits (bin)\n",
          cmd, cmd, cmd, cmd, cmd, cmd, cmd);
  return 1;
}

namespace {
struct svnbaserev {
  unsigned char bytes[4] = {0};

  svnbaserev() = default;
  explicit svnbaserev(int rev) { set_rev(rev); }
  static svnbaserev make_from_binary(const unsigned char *bytes);
  int get_rev() const;
  void set_rev(int rev);
};
} // end namespace

svnbaserev svnbaserev::make_from_binary(const unsigned char *bytes) {
  svnbaserev sbr;
  std::memcpy(sbr.bytes, bytes, 4);
  return sbr;
}
int svnbaserev::get_rev() const {
  unsigned data = 0;
  data |= unsigned(bytes[0]) << 24;
  data |= unsigned(bytes[1]) << 16;
  data |= unsigned(bytes[2]) << 8;
  data |= unsigned(bytes[3]);
  return static_cast<int>(data);
}
void svnbaserev::set_rev(int rev) {
  assert(rev >= 0);
  unsigned data = static_cast<unsigned>(rev);
  bytes[0] = 0xff & (data >> 24);
  bytes[1] = 0xff & (data >> 16);
  bytes[2] = 0xff & (data >> 8);
  bytes[3] = 0xff & (data);
}

namespace {
// Some constants.
static constexpr const long magic_size = 8;
static constexpr const long num_root_bits = 14;
static constexpr const long num_subtrie_bits = 6;
static constexpr const long root_index_bitmap_offset = magic_size;

static_assert(sizeof(binary_sha1) == 20);
template <class T> struct data_entry_impl {
  static constexpr const long value_size = sizeof(typename T::value_type);
  static constexpr const long size = sizeof(binary_sha1) + value_size;
};

struct commits_table : data_entry_impl<commits_table> {
  static constexpr const long table_offset = magic_size;
  typedef binary_sha1 value_type;
  static constexpr const char *const table_name = "commits";
  static constexpr const char *const key_name = "split";
  static constexpr const char *const value_name = "mono";

  static std::string to_dump_string(const binary_sha1 &bin) {
    textual_sha1 text(bin);
    return text.bytes;
  }
};

struct svnbase_table : data_entry_impl<svnbase_table> {
  static constexpr const long table_offset = magic_size;
  typedef svnbaserev value_type;
  static constexpr const char *const table_name = "svnbase";
  static constexpr const char *const key_name = "sha1";
  static constexpr const char *const value_name = "rev";

  static std::string to_dump_string(const svnbaserev &bin) {
    return std::to_string(bin.get_rev());
  }
};

struct index_entry {
  static constexpr const long size = 3;

  unsigned char bytes[size] = {0};

  index_entry() = default;
  index_entry(bool is_data, int offset);

  bool is_data() const;
  int num() const;
};
} // end namespace

static constexpr long compute_index_bitmap_size(long num_bits) {
  return 1ull << (num_bits - 3);
}
static constexpr long compute_index_entries_size(long num_bits) {
  return (1ull << num_bits) * index_entry::size;
}
static constexpr const long root_index_entries_offset =
    root_index_bitmap_offset + compute_index_bitmap_size(num_root_bits);
static constexpr const long subtrie_indexes_offset =
    root_index_entries_offset + compute_index_entries_size(num_root_bits);
static constexpr const long subtrie_index_bitmap_offset = 0;
static constexpr const long subtrie_index_entries_offset =
    compute_index_bitmap_size(num_subtrie_bits);
static constexpr const long subtrie_index_size =
    subtrie_index_entries_offset + compute_index_entries_size(num_subtrie_bits);

namespace {
struct upstream_entry {
  std::string name;
  long num_upstreams = -1;
  long commits_size = -1;
  long svnbase_size = -1;
};
class file_stream {
  FILE *stream = nullptr;
  mmapped_file mmapped;
  size_t num_bytes = -1;
  bool is_stream = false;
  bool is_initialized = false;
  long position = 0;

public:
  file_stream() = default;
  int init(int fd, bool is_read_only) {
    return is_read_only ? init_mmap(fd) : init_stream(fd);
  }
  int init_stream(int fd);
  int init_mmap(int fd);

  size_t get_num_bytes() const { return num_bytes; }

  int seek_end();
  long tell();
  int seek(long pos);
  int read(unsigned char *bytes, int count);
  int seek_and_read(long pos, unsigned char *bytes, int count);
  int write(const unsigned char *bytes, int count);

  int close();
  ~file_stream() { close(); }
};
struct split2monodb {
  struct table_streams {
    std::string name;
    file_stream data;
    file_stream index;

    explicit table_streams(std::string &&name) : name(std::move(name)) {}

    int init(int dbfd, bool is_read_only, const unsigned char *magic,
             int record_offset, int record_size);
    int close_files();

    ~table_streams() { close_files(); }
  };

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

struct bitmap_ref {
  int byte_offset = 0;
  int bit_offset = 0;
  unsigned char byte = 0;

  void initialize(int bitmap_offset, int i);
  void initialize_and_set(int bitmap_offset, int i);
  static bool get_bit(unsigned char byte, int bit_offset);
  bool get_bit() const { return get_bit(byte, bit_offset); }
  void set_bit();
};
struct index_query {
  struct in_data {
    binary_sha1 sha1;
    int start_bit = 0;
    int num_bits = num_root_bits;
    int bitmap_offset = root_index_bitmap_offset;
    int entries_offset = root_index_entries_offset;

    explicit in_data(const binary_sha1 &sha1) : sha1(sha1) {}
    explicit in_data(const textual_sha1 &sha1) : sha1(sha1) {}
  };
  struct out_data {
    bitmap_ref bits;
    index_entry entry;
    int entry_offset = 0;

    bool found = false;
  };

  in_data in;
  out_data out;

  explicit index_query(const textual_sha1 &sha1) : in(sha1) {}
  explicit index_query(const binary_sha1 &sha1) : in(sha1) {}
  static index_query from_binary(const unsigned char *key);
  static index_query from_textual(const char *key);

  int lookup(file_stream &index);
  int lookup_impl(file_stream &index);
  int num_bits_so_far() const;
  int advance();
  int insert_new_entry(file_stream &index, int new_num) const;
  int update_after_collision(file_stream &index, int new_num,
                             const binary_sha1 &existing_sha1,
                             int existing_num) const;
};
template <class T> struct data_query : index_query {
  typedef T table_type;
  typedef typename table_type::value_type value_type;

  bool found_data = false;
  binary_sha1 found_sha1;
  int data_offset = 0;

  data_query(index_query &&q) : index_query(std::move(q)) {}
  explicit data_query(const textual_sha1 &sha1) : index_query(sha1) {}
  explicit data_query(const binary_sha1 &sha1) : index_query(sha1) {}
  static data_query from_binary(const unsigned char *key) {
    return index_query::from_binary(key);
  }
  static data_query from_textual(const char *key) {
    return index_query::from_textual(key);
  }

  typedef split2monodb::table_streams table_streams;
  int lookup_data_impl(table_streams &ts);
  int lookup_data(table_streams &ts, value_type &value);
  int insert_data(table_streams &ts, const value_type &value);
  int insert_data_impl(table_streams &ts, const value_type &value);

  int insert_new_entry(table_streams &ts, int new_num) const {
    return index_query::insert_new_entry(ts.index, new_num);
  }
  int update_after_collision(table_streams &ts, int new_num) const {
    int existing_num =
        (this->data_offset - table_type::table_offset) / table_type::size;
    assert((this->data_offset - table_type::table_offset) % table_type::size ==
           0);
    return index_query::update_after_collision(ts.index, new_num, found_sha1,
                                               existing_num);
  }
};
typedef data_query<commits_table> commits_query;
typedef data_query<svnbase_table> svnbase_query;
} // end namespace

int file_stream::init_stream(int fd) {
  assert(fd != -1);
  assert(!is_initialized);
  if (!(stream = fdopen(fd, "w+b")) || fseek(stream, 0, SEEK_END) ||
      (num_bytes = ftell(stream)) == -1 || fseek(stream, 0, SEEK_SET)) {
    ::close(fd);
    return 1;
  }
  is_initialized = true;
  is_stream = true;
  return 0;
}
int file_stream::init_mmap(int fd) {
  assert(!is_initialized);
  is_initialized = true;
  is_stream = false;
  mmapped.init(fd);
  num_bytes = mmapped.num_bytes;
  return 0;
}
int file_stream::seek_end() {
  assert(is_initialized);
  if (is_stream)
    return fseek(stream, 0, SEEK_END);
  position = num_bytes;
  return 0;
}
long file_stream::tell() {
  assert(is_initialized);
  if (is_stream)
    return ftell(stream);
  return position;
}
static void limit_position(long &pos, int &count, size_t &num_bytes) {}
int file_stream::seek_and_read(long pos, unsigned char *bytes, int count) {
  assert(is_initialized);
  // Check that the position is valid first.
  if (position >= num_bytes)
    return 0;
  if (pos > num_bytes || seek(pos))
    return 0;
  if (count + pos > num_bytes)
    count = num_bytes - pos;
  if (!count)
    return 0;
  return read(bytes, count);
}
int file_stream::seek(long pos) {
  assert(is_initialized);
  if (is_stream)
    return fseek(stream, pos, SEEK_SET);
  if (pos > num_bytes)
    return 1;
  position = pos;
  return 0;
}
int file_stream::read(unsigned char *bytes, int count) {
  assert(is_initialized);
  if (is_stream)
    return fread(bytes, 1, count, stream);
  if (position + count > num_bytes)
    count = num_bytes - position;
  if (count > 0)
    std::memcpy(bytes, mmapped.bytes + position, count);
  position += count;
  return count;
}
int file_stream::write(const unsigned char *bytes, int count) {
  assert(is_initialized);
  assert(is_stream);
  return fwrite(bytes, 1, count, stream);
}

int file_stream::close() {
  if (!is_initialized)
    return 0;
  is_initialized = false;
  if (is_stream)
    return fclose(stream);
  return mmapped.close();
}

int split2monodb::table_streams::close_files() {
  return data.close() | index.close();
}
int split2monodb::close_files() {
  return commits.close_files() | svnbase.close_files();
}

bool index_entry::is_data() const { return bytes[0] >> 7; }

int index_entry::num() const {
  unsigned data = 0;
  data |= bytes[0] << 16;
  data |= bytes[1] << 8;
  data |= bytes[2];
  return data & ((1ull << 23) - 1);
}

index_entry::index_entry(bool is_data, int num) {
  assert(num >= 0);
  assert(num < (1 << 23));
  bytes[0] = static_cast<int>(is_data) << 7 | num >> 16;
  bytes[1] = (num >> 8) & 0xff;
  bytes[2] = num & 0xff;
}

index_query index_query::from_binary(const unsigned char *key) {
  binary_sha1 sha1;
  sha1.from_binary(key);
  index_query q(sha1);
  return q;
}

index_query index_query::from_textual(const char *key) {
  binary_sha1 sha1;
  sha1.from_textual(key);
  index_query q(sha1);
  return q;
}

int index_query::num_bits_so_far() const {
  return in.start_bit ? in.start_bit + num_subtrie_bits : num_root_bits;
}

int index_query::advance() {
  if (num_bits_so_far() + num_subtrie_bits > 160)
    return error("cannot resolve hash collision");

  unsigned subtrie = out.entry.num();
  unsigned subtrie_offset =
      subtrie_indexes_offset + subtrie_index_size * subtrie;
  in.bitmap_offset = subtrie_offset + subtrie_index_bitmap_offset;
  in.entries_offset = subtrie_offset + subtrie_index_entries_offset;
  in.start_bit += in.num_bits;
  in.num_bits = num_subtrie_bits;
  return 0;
}

void bitmap_ref::initialize(int bitmap_offset, int i) {
  byte_offset = bitmap_offset + i / 8;
  bit_offset = i % 8;
  byte = 0;
}
void bitmap_ref::initialize_and_set(int bitmap_offset, int i) {
  initialize(bitmap_offset, i);
  set_bit();
}
bool bitmap_ref::get_bit(unsigned char byte, int bit_offset) {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  return byte & (0x100 >> (bit_offset + 1)) ? 1 : 0;
}

void bitmap_ref::set_bit() {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  byte |= 0x100 >> (bit_offset + 1);
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

int split2monodb::table_streams::init(int dbfd, bool is_read_only,
                                      const unsigned char *magic,
                                      int record_offset, int record_size) {
  int flags = is_read_only ? O_RDONLY : (O_RDWR | O_CREAT);
  std::string index_name = name + ".magic";
  int datafd = openat(dbfd, name.c_str(), flags);
  int indexfd = openat(dbfd, index_name.c_str(), flags);
  int has_error = 0;
  if (datafd == -1)
    has_error |= is_read_only ? 1 : error("could not open <dbdir>/" + name);
  if (indexfd == -1)
    has_error |=
        is_read_only ? 1 : error("could not open <dbdir>/" + index_name);
  if (has_error) {
    close(indexfd);
    close(datafd);
    return 1;
  }
  if (!is_read_only) {
    fchmod(indexfd, 0644);
    fchmod(datafd, 0644);
  }

  if (data.init(datafd, is_read_only))
    return error("could not open <dbdir>/" + name);
  if (index.init(indexfd, is_read_only))
    return error("could not open <dbdir>/" + index_name);

  // Check that file sizes make sense.
  const unsigned char index_magic[] = {'s', 2, 'm', 0x1, 'n', 0xd, 0xe, 'x'};
  assert(sizeof(index_magic) == magic_size);
  if (data.get_num_bytes()) {
    if (!index.get_num_bytes())
      return error("unexpected data without index for " + name);
    if (data.get_num_bytes() < magic_size ||
        (data.get_num_bytes() - record_offset) % record_size)
      return error("invalid data for " + name);

    unsigned char file_magic[magic_size];
    if (data.seek(0) || data.read(file_magic, magic_size) != magic_size ||
        memcmp(file_magic, magic, magic_size))
      return error("bad magic for " + name);
  } else if (!is_read_only) {
    if (data.seek(0) || data.write(magic, magic_size) != magic_size)
      return error("could not write magic for " + name);
  }
  if (index.get_num_bytes()) {
    if (!data.get_num_bytes())
      return error("unexpected index without " + name);
    if (index.get_num_bytes() < magic_size)
      return error("invalid index for " + name);

    unsigned char file_magic[magic_size];
    if (index.seek(0) || index.read(file_magic, magic_size) != magic_size ||
        memcmp(file_magic, index_magic, magic_size))
      return error("bad index magic for " + name);
  } else if (!is_read_only) {
    if (index.seek(0) || index.write(index_magic, magic_size) != magic_size)
      return error("could not write index magic for " + name);
  }
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

int index_query::lookup_impl(file_stream &index) {
  out.found = false;
  unsigned i = in.sha1.get_bits(in.start_bit, in.num_bits);
  out.entry_offset = in.entries_offset + i * index_entry::size;
  out.bits.initialize(in.bitmap_offset, i);

  // Not found.  Be resilient to an unwritten bitmap.
  if (index.seek_and_read(out.bits.byte_offset, &out.bits.byte, 1) != 1 ||
      !out.bits.get_bit())
    return 0;

  out.found = true;
  if (index.seek_and_read(out.entry_offset, out.entry.bytes,
                          index_entry::size) != index_entry::size)
    return 1;
  return 0;
}

int index_query::lookup(file_stream &index) {
  if (lookup_impl(index))
    return 1;
  if (!out.found)
    return 0;
  while (!out.entry.is_data()) {
    advance();
    if (lookup_impl(index))
      return 1;
    if (!out.found)
      return 0;
  }
  return 0;
}

template <class T> int data_query<T>::lookup_data_impl(table_streams &ts) {
  if (lookup(ts.index))
    return 1;
  if (!out.found)
    return 0;

  // Look it up in the commits list.
  int i = out.entry.num();
  data_offset = table_type::table_offset + table_type::size * i;
  if (ts.data.seek_and_read(data_offset, found_sha1.bytes, 20) != 20)
    return 1;
  if (in.sha1 == found_sha1)
    found_data = true;
  return 0;
}

template <class T>
int data_query<T>::lookup_data(table_streams &ts, value_type &value) {
  if (lookup_data_impl(ts))
    return error("problem looking up split commit");
  if (!found_data)
    return 1;
  if (ts.data.seek_and_read(data_offset + 20, value.bytes, T::value_size) !=
      T::value_size)
    return error("could not extract mono commit");
  return 0;
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

int index_query::insert_new_entry(file_stream &index, int new_num) const {
  // update the existing trie/subtrie
  index_entry entry(/*is_data=*/true, new_num);
  if (index.seek(out.entry_offset) ||
      index.write(entry.bytes, index_entry::size) != index_entry::size)
    return error("could not write index entry");

  // update the bitmap
  bitmap_ref new_bits = out.bits;
  new_bits.set_bit();
  if (index.seek(new_bits.byte_offset) || index.write(&new_bits.byte, 1) != 1)
    return error("could not update index bitmap");
  return 0;
}

int index_query::update_after_collision(file_stream &index, int new_num,
                                        const binary_sha1 &existing_sha1,
                                        int existing_num) const {
  // add subtrie(s) with full contents so far
  // TODO: add test that covers this.
  int first_mismatched_bit = in.sha1.get_mismatched_bit(existing_sha1);
  assert(first_mismatched_bit < 160);
  int num_bits_so_far = this->num_bits_so_far();
  assert(first_mismatched_bit >= num_bits_so_far - num_subtrie_bits);

  // Make new subtries.
  struct trie_update_stack {
    bool skip_bitmap_update = false;
    bitmap_ref bits;

    int entry_offset = 0;
    bool is_data = false;
    int num = 0;
  };

  if (index.seek_end())
    return error("could not seek to end to discover num subtries");
  int end_offset = index.tell();
  int next_subtrie =
      end_offset <= subtrie_indexes_offset
          ? 0
          : 1 + (end_offset - subtrie_indexes_offset - 1) / subtrie_index_size;

  // Update index in reverse, so that if this gets aborted early (or killed)
  // the output file has no semantic changes.
  trie_update_stack stack[160 / num_subtrie_bits + 2];
  trie_update_stack *top = stack;

  // Start with updating the existing trie that is pointing at the conflicting
  // commit.  Note that the bitmap is already set, we just need to make it
  // point at the right place.
  top->skip_bitmap_update = true;
  top->entry_offset = out.entry_offset;
  top->num = next_subtrie++;

  // Add some variables that need to last past the while loop.
  int subtrie_offset, bitmap_offset;
  int n, n_entry_offset;
  int f, f_entry_offset;
  while (true) {
    // Calculate the entries for the next subtrie.
    subtrie_offset = subtrie_indexes_offset + top->num * subtrie_index_size;
    bitmap_offset = subtrie_offset + subtrie_index_bitmap_offset;
    n = in.sha1.get_bits(num_bits_so_far, num_subtrie_bits);
    f = existing_sha1.get_bits(num_bits_so_far, num_subtrie_bits);
    n_entry_offset =
        subtrie_offset + subtrie_index_entries_offset + n * index_entry::size;
    f_entry_offset =
        subtrie_offset + subtrie_index_entries_offset + f * index_entry::size;

    if (n != f)
      break;

    // push another subtrie.
    num_bits_so_far += num_subtrie_bits;

    ++top;
    top->num = next_subtrie++;
    top->entry_offset = n_entry_offset;
    top->bits.initialize_and_set(bitmap_offset, n);
    assert(top->bits.byte);
  }

  // found a difference.  add commit entries to last subtrie.
  int fbyte_offset = bitmap_offset + f / 8;
  int nbyte_offset = bitmap_offset + n / 8;
  ++top;
  top->is_data = true;
  top->num = existing_num;
  top->entry_offset = f_entry_offset;
  top->bits.initialize_and_set(bitmap_offset, f);
  assert(top->bits.byte);

  ++top;
  top->is_data = true;
  top->num = new_num;
  top->entry_offset = n_entry_offset;
  top->bits.initialize_and_set(bitmap_offset, n);
  top->skip_bitmap_update = top[-1].bits.byte_offset == top->bits.byte_offset;
  assert(top->bits.byte);
  if (top->skip_bitmap_update)
    top[-1].bits.byte |= top->bits.byte;

  // Unwind the stack.  Be careful not to decrement top past the beginning.
  ++top;
  while (top != stack) {
    --top;

    // Update the index entry.
    index_entry entry(top->is_data, top->num);
    if (index.seek(top->entry_offset) ||
        index.write(entry.bytes, index_entry::size) != index_entry::size)
      return error("could not write index entry");

    if (top->skip_bitmap_update)
      continue;

    // Update the bitmap to point at the index entry.
    if (index.seek(top->bits.byte_offset) ||
        index.write(&top->bits.byte, 1) != 1)
      return error("could not write to index bitmap");
  }

  return 0;
}

template <class T>
int data_query<T>::insert_data_impl(table_streams &ts,
                                    const value_type &value) {
  bool need_new_subtrie = data_offset ? true : false;

  // add the commit to *commits*
  if (ts.data.seek_end())
    return error("could not seek in commits");
  int new_data_offset = ts.data.tell();
  int new_num = (new_data_offset - table_type::table_offset) / table_type::size;
  assert((new_data_offset - table_type::table_offset) % table_type::size == 0);
  if (ts.data.write(in.sha1.bytes, 20) != 20 ||
      ts.data.write(value.bytes, table_type::value_size) !=
          table_type::value_size)
    return error("could not write commits");

  if (!need_new_subtrie)
    return insert_new_entry(ts, new_num);

  return update_after_collision(ts, new_num);
}

template <class T>
int data_query<T>::insert_data(table_streams &ts, const value_type &value) {
  if (lookup_data_impl(ts))
    return error("index issue");
  assert(out.entry_offset);

  if (found_data)
    return error("sha1 is already mapped");

  return insert_data_impl(ts, value);
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
    return 1;

  return commits_query(split).insert_data(db.commits, binary_sha1(mono));
}

static int main_insert_stdin(const char *cmd, const char *dbdir) {
  split2monodb db;
  if (db.opendb(dbdir))
    return 1;

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

template <class T>
static int merge_tables(split2monodb::table_streams &main, size_t recorded_size,
                        split2monodb::table_streams &upstream,
                        size_t actual_size) {
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

  if (existing_entry->second.num_upstreams > upstream.upstreams.size())
    return error("upstream is missing upstreams we already merged");

  if (existing_entry->second.commits_size > upstream.commits_size())
    return error("upstream is missing commits we already merged");

  if (existing_entry->second.svnbase_size > upstream.svnbase_size())
    return error("upstream is missing svnbase revs we already merged");

  // Nothing to do if nothing has changed (or the upstream is empty).
  if (existing_entry->second.num_upstreams == upstream.upstreams.size() &&
      existing_entry->second.commits_size == upstream.commits_size() &&
      existing_entry->second.svnbase_size == upstream.svnbase_size())
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
  if (merge_tables<commits_table>(main.commits,
                                  existing_entry->second.commits_size,
                                  upstream.commits, upstream.commits_size()) ||
      merge_tables<svnbase_table>(main.svnbase,
                                  existing_entry->second.svnbase_size,
                                  upstream.svnbase, upstream.svnbase_size()))
    return 1;

  // Close the streams.
  if (main.close_files())
    return error("error closing commits or index after writing");

  // Merge upstreams.
  existing_entry->second.commits_size = upstream.commits_size();
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

static int dump_index(file_stream &index, const char *name, int num) {
  int num_bits = num == -1 ? num_root_bits : num_subtrie_bits;
  int bitmap_size_in_bits = 1u << num_bits;
  int bitmap_offset = num == -1
                          ? root_index_bitmap_offset
                          : subtrie_indexes_offset + subtrie_index_size * num +
                                subtrie_index_bitmap_offset;
  int entries_offset = num == -1
                           ? root_index_entries_offset
                           : subtrie_indexes_offset + subtrie_index_size * num +
                                 subtrie_index_entries_offset;

  // Visit bitmap, and print out entries.
  unsigned char bitmap[(1u << num_root_bits) / 8];
  if (index.seek_and_read(bitmap_offset, bitmap, bitmap_size_in_bits / 8) !=
      bitmap_size_in_bits / 8)
    return 1;
  if (num == -1)
    printf("%s index num=root num-bits=%02d\n", name, num_bits);
  else
    printf("%s index num=%04d num-bits=%02d\n", name, num, num_bits);
  int any = 0;
  for (int i = 0, ie = bitmap_size_in_bits / 8; i != ie; ++i) {
    if (!bitmap[i])
      continue;
    for (int bit = 0; bit != 8; ++bit) {
      if (!bitmap_ref::get_bit(bitmap[i], bit))
        continue;

      any = 1;
      int entry_i = i * 8 + bit;
      int offset = entries_offset + index_entry::size * entry_i;
      index_entry entry;
      if (index.seek_and_read(offset, entry.bytes, index_entry::size) !=
          index_entry::size)
        return 1;

      char bits[num_root_bits + 1] = {0};
      for (int i = 0; i < num_bits; ++i)
        bits[i] = entry_i & ((1u << (num_bits - i)) >> 1) ? '1' : '0';

      int entry_num = entry.num();
      if (entry.is_data())
        printf("  entry: bits=%s table=%08d\n", bits, entry_num);
      else
        printf("  entry: bits=%s index=%04d\n", bits, entry_num);
    }
  }
  if (!any)
    error("no bits set in index...");
  return 0;
}

template <class T> static int dump_table(split2monodb::table_streams &ts) {
  typedef T table_type;
  typedef typename table_type::value_type value_type;

  // Print the table.
  if (ts.data.seek(table_type::table_offset))
    return error("could not read any commit pairs");
  printf("%s table\n", table_type::table_name);
  int i = 0;
  binary_sha1 key;
  value_type value;
  while (ts.data.seek_and_read(table_type::table_offset + i * table_type::size,
                               key.bytes, 20) == 20 &&
         ts.data.seek_and_read(
             table_type::table_offset + i * table_type::size + 20, value.bytes,
             table_type::value_size) == table_type::value_size) {
    textual_sha1 dump_key(key);
    std::string dump_value = table_type::to_dump_string(value);
    printf("  %08d: %s=%s %s=%s\n", i++, table_type::key_name, dump_key.bytes,
           table_type::value_name, dump_value.c_str());
  }
  if (!i)
    printf("  <empty>\n");
  printf("\n");

  // Print the indexes, starting with the root (-1).
  i = -1;
  while (!dump_index(ts.index, table_type::table_name, i))
    ++i;
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

template <class T>
static int forward_to_reader(void *context, std::string line) {
  return (*reinterpret_cast<T *>(context))(std::move(line));
}

template <class T>
static int forward_to_writer(void *context, FILE *file, bool &stop) {
  return (*reinterpret_cast<T *>(context))(file, stop);
}

extern char **environ;

typedef int (*git_reader)(void *, std::string);
typedef int (*git_writer)(void *, FILE *, bool &);
static int call_git(char *argv[], git_reader reader, void *rcontext,
                    git_writer writer, void *wcontext) {
  if (strcmp(argv[0], "git"))
    return error("wrong git executable");

  struct cleanup {
    posix_spawn_file_actions_t *file_actions = nullptr;
    ~cleanup() {
      if (file_actions)
        posix_spawn_file_actions_destroy(file_actions);
    }
    int set(posix_spawn_file_actions_t &file_actions) {
      this->file_actions = &file_actions;
      return 0;
    }
  } cleanup;

  int fromgit[2];
  int togit[2];
  pid_t pid = -1;
  posix_spawn_file_actions_t file_actions;

  if (pipe(fromgit) || posix_spawn_file_actions_init(&file_actions) ||
      cleanup.set(file_actions) ||
      posix_spawn_file_actions_addclose(&file_actions, fromgit[0]) ||
      posix_spawn_file_actions_adddup2(&file_actions, fromgit[1], 1) ||
      (writer ? (pipe(togit) ||
                 posix_spawn_file_actions_addclose(&file_actions, togit[1]) ||
                 posix_spawn_file_actions_adddup2(&file_actions, togit[0], 0))
              : posix_spawn_file_actions_addclose(&file_actions, 0)) ||
      posix_spawn(&pid, argv[0], &file_actions, nullptr, argv, environ) ||
      close(fromgit[1]) || (writer && close(togit[0])))
    return error("failed to spawn git");

  if (writer) {
    FILE *file = fdopen(fromgit[1], "1");
    if (!file)
      return error("failed to open stream to git");
    bool stop = false;
    while (!stop)
      if (writer(wcontext, file, stop)) {
        fclose(file);
        return 1;
      }
    if (fclose(file))
      return error("problem closing pipe writing to git");
  }

  FILE *file = fdopen(fromgit[0], "r");
  if (!file)
    return error("failed to open stream from git");

  size_t length = 0;
  while (char *line = fgetln(file, &length)) {
    if (!length || line[length - 1] != '\n') {
      fclose(file);
      return error("expected newline");
    }
    if (reader(rcontext, std::string(line, line + length - 1))) {
      fclose(file);
      return 1;
    }
  }
  if (!feof(file)) {
    fclose(file);
    return error("failed to read from git");
  }

  int status = 0;
  if (fclose(file) || wait(&status) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status))
    return error("git failed");

  return 0;
}
template <class T> static int call_git(const char *argv[], T reader) {
  return call_git(const_cast<char **>(argv), forward_to_reader<T>,
                  reinterpret_cast<void *>(&reader), nullptr, nullptr);
}
template <class T, class U>
static int call_git(const char *argv[], T reader, U writer) {
  return call_git(const_cast<char **>(argv), forward_to_reader<T>,
                  reinterpret_cast<void *>(&reader), forward_to_writer<U>,
                  reinterpret_cast<void *>(&writer));
}

namespace {
struct fparent_commit {
  sha1_ref commit;
  long long ct = 0;
};
struct generic_commit {
  sha1_ref commit;
  sha1_ref tree;

  union {
    sha1_ref small[2];
    std::unique_ptr<sha1_ref[]> large;
  } parents_storage;
  int num_parents = 0;
  sha1_ref *parents = nullptr;
};

struct fparent_commit_range {
  fparent_commit *first;
  fparent_commit *last;
};
struct generic_commit_range {
  generic_commit *first;
  generic_commit *last;
};
struct commit_source {
  fparent_commit_range fps;
  generic_commit_range all;

  std::string dir;
};

struct git_tree {
  struct item_type {
    sha1_ref sha1;
    std::string name;
    bool is_tree = false;
    bool is_exec = false;
  };
  sha1_ref sha1;
  item_type *items = nullptr;
  int num_items = 0;
};
struct tree_cache {
  void note(const sha1_ref &commit, const sha1_ref &tree);
  void note(const git_tree &tree);
  int get_tree(const sha1_ref &commit, sha1_ref &tree);
  int ls_tree(git_tree &tree);
  int mktree(git_tree &sha1);

  struct git_commit {
    sha1_ref commit;
    sha1_ref tree;
  };
  static constexpr const int num_cache_bits = 16;
  git_tree trees[1u << num_cache_bits];
  git_commit commits[1u << num_cache_bits];
  sha1_pool &pool;
};
} // end namespace

void tree_cache::note(const sha1_ref &commit, const sha1_ref &tree) {
  assert(commit.sha1);
  assert(tree.sha1);
  auto &entry = commits[commit.sha1->get_bits(0, num_cache_bits)];
  entry.commit = commit;
  entry.tree = tree;
}

void tree_cache::note(const git_tree &tree) {
  assert(tree.sha1.sha1);
  trees[tree.sha1.sha1->get_bits(0, num_cache_bits)] = tree;
}

int tree_cache::get_tree(const sha1_ref &commit, sha1_ref &tree) {
  assert(commit.sha1);
  auto &entry = commits[commit.sha1->get_bits(0, num_cache_bits)];
  if (entry.commit == commit) {
    tree = entry.tree;
    return 0;
  }

  bool once = false;
  auto reader = [&](std::string line) {
    if (once)
      return 1;
    once = true;

    textual_sha1 text;
    if (text.from_input(line.c_str()))
      return 1;

    tree = pool.lookup(text);
    note(commit, tree);
    return 0;
  };

  assert(tree.sha1);
  std::string ref = textual_sha1(*tree.sha1).bytes;
  ref += "^{tree}";
  const char *argv[] = {"git", "rev-parse", "--verify", ref.c_str(), nullptr};
  return call_git(argv, reader);
}

int tree_cache::ls_tree(git_tree &tree) {
  assert(tree.sha1.sha1);
  auto &entry = trees[tree.sha1.sha1->get_bits(0, num_cache_bits)];
  if (entry.sha1 == tree.sha1) {
    tree = entry;
    return 0;
  }

  constexpr const int max_items = 64;
  git_tree::item_type items[max_items];
  git_tree::item_type *last = items;
  auto reader = [&](std::string line) {
    if (last - items == max_items)
      return 1;

    size_t space1 = line.find(' ');
    size_t space2 = line.find(' ', space1 + 1);
    size_t tab = line.find('\t', space2 + 1);
    if (!line.compare(0, space1, "100755"))
      last->is_exec = true;
    else if (line.compare(0, space1, "100644"))
      return 1;

    if (!line.compare(space1 + 1, space2 - space1 - 1, "tree"))
      last->is_tree = true;
    else if (line.compare(space1 + 1, space2 - space1 - 1, "blob"))
      return 1;

    last->name = line.substr(tab + 1);
    if (last->name.empty())
      return 1;

    textual_sha1 text;
    line[tab] = '\0';
    if (text.from_input(&line[space2 + 1]))
      return 1;
    last->sha1 = pool.lookup(text);
    ++last;
    return 0;
  };

  std::string ref = textual_sha1(*tree.sha1.sha1).bytes;
  const char *args[] = {"git", "ls-tree", ref.c_str(), nullptr};
  return error("ls_tree: not implemented");
  if (call_git(args, reader))
    return 1;

  tree.num_items = last - items;
  tree.items = new (pool.alloc) git_tree::item_type[tree.num_items];
  std::move(items, last, tree.items);
  note(tree);
  return 0;
}

int tree_cache::mktree(git_tree &tree) {
  assert(!tree.sha1.sha1);
  bool once = false;
  auto reader = [&](std::string line) {
    if (once)
      return 1;
    once = true;

    textual_sha1 text;
    if (text.from_input(line.c_str()))
      return 1;

    tree.sha1 = pool.lookup(text);
    note(tree);
    return 0;
  };

  auto writer = [&](FILE *file, bool &stop) {
    assert(!stop);
    for (auto i = 0; i != tree.num_items; ++i) {
      assert(tree.items[i].sha1.sha1);
      if (!fprintf(file, "%s %s %s\t%s\n",
                   tree.items[i].is_exec ? "100755" : "100644",
                   tree.items[i].is_tree ? "tree" : "blob",
                   textual_sha1(*tree.items[i].sha1.sha1).bytes,
                   tree.items[i].name.c_str()))
        return 1;
    }
    stop = true;
    return 0;
  };

  const char *argv[] = {"git", "mktree", nullptr};
  return call_git(argv, reader, writer);
}

static int lookup_svnbaserev(const binary_sha1 &sha1, svnbaserev &rev) {
  return error(std::string(__func__) + " not implemented");
}

static int main_translate_commits(const char *cmd, int argc,
                                  const char *argv[]) {
  if (argc != 1)
    return usage("translate-commits: extra positional arguments", cmd);
  split2monodb db;
  if (db.opendb(argv[0]))
    return usage("could not open <dbdir>", cmd);

  return error(std::string(__func__) + " not implemented");
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
  SUB_MAIN_IMPL("translate-commits", translate_commits);
#undef SUB_MAIN_IMPL
#undef SUB_MAIN
#undef SUB_MAIN_SVNBASE
}
