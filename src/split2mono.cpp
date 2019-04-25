// split2mono.cpp
//
// Format description below:
//
// tree: split2mono
// - blob: commits (file size: num-commits)
//   0x0000-0x0007: magic
//   0x0008-0x...: commit pairs
//   - commit: 0x28
//     0x00-0x13: split
//     0x14-0x27: mono
//
// - blob: repos (text file)
//   <header>
//   <upstream>...
//
//   <header>::   'name:' SP <name> LF
//   <upstream>:: 'upstream:' SP <name> SP <num-commits> SP <num-upstreams> LF
//
// - blob: index
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
#include <string>
#include <vector>

static int show_progress(int n, int total) {
  return fprintf(stderr, "   %9d / %d commits mapped\n", n, total) < 0;
}
static int error(const char *msg) {
  fprintf(stderr, "error: %s\n", msg);
  return 1;
}

static int usage(const char *msg, const char *cmd) {
  error(msg);
  if (const char *slash = strrchr(cmd, '/'))
    cmd = slash + 1;
  fprintf(stderr,
          "usage: %s create <dbdir>\n"
          "       %s lookup <dbdir> <split>\n"
          "       %s upstream <dbdir> <upstream-dbdir>\n"
          "       %s insert <dbdir> [<split> <mono>]\n"
          "       %s dump <dbdir>\n"
          "\n"
          "   <dbdir>/upstreams: merged upstreams (text)\n"
          "   <dbdir>/commits: translated commits (bin)\n"
          "   <dbdir>/index: translated commits (bin)\n",
          cmd, cmd, cmd, cmd, cmd);
  return 1;
}

static int check_sha1(const char *sha1) {
  const char *ch = sha1;
  for (; *ch; ++ch) {
    // Allow "[0-9a-z]".
    if (*ch >= '0' && *ch <= '9')
      continue;
    if (*ch >= 'a' && *ch <= 'z')
      continue;
    return 1;
  }
  return ch - sha1 == 40 ? 0 : 1;
}

// Some constants.
constexpr const long magic_size = 8;
constexpr const long commit_pairs_offset = magic_size;
constexpr const long commit_pair_size = 40;
constexpr const long num_root_bits = 14;
constexpr const long num_subtrie_bits = 6;
constexpr const long root_index_bitmap_offset = magic_size;
constexpr const long index_entry_size = 3;
constexpr long compute_index_bitmap_size(long num_bits) {
  return 1ull << (num_bits - 3);
}
constexpr long compute_index_entries_size(long num_bits) {
  return (1ull << num_bits) * index_entry_size;
}
constexpr const long root_index_entries_offset =
    root_index_bitmap_offset + compute_index_bitmap_size(num_root_bits);
constexpr const long subtrie_indexes_offset =
    root_index_entries_offset + compute_index_entries_size(num_root_bits);
constexpr const long subtrie_index_bitmap_offset = 0;
constexpr const long subtrie_index_entries_offset =
    compute_index_bitmap_size(num_subtrie_bits);
constexpr const long subtrie_index_size =
    subtrie_index_entries_offset + compute_index_entries_size(num_subtrie_bits);

namespace {
struct upstream_entry {
  std::string name;
  long num_commits = -1;
  long num_upstreams = -1;
};
class stream_gimmick {
  FILE *stream = nullptr;
  mmapped_file mmapped;
  size_t num_bytes = -1;
  bool is_stream = false;
  bool is_initialized = false;
  long position = 0;

public:
  stream_gimmick() = default;
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
  ~stream_gimmick() { close(); }
};
struct split2monodb {
  bool is_verbose = false;
  stream_gimmick commits;
  stream_gimmick index;
  bool is_read_only = false;

  bool has_read_upstreams = false;
  int upstreamsfd = -1;
  int dbfd = -1;
  std::string name;

  // FIXME: std::map is way overkill, we just have a few of these.
  std::map<std::string, upstream_entry> upstreams;

  int opendb(const char *dbdir);
  int parse_upstreams();
  long num_commits() const {
    return (commits.get_num_bytes() - commit_pairs_offset) / commit_pair_size;
  }

  int close_files();

  void log(std::string x) {
    if (!is_verbose)
      return;
    fprintf(stderr, "log: %s\n", x.c_str());
  }

  ~split2monodb() { close_files(); }
};
struct index_entry {
  unsigned char bytes[index_entry_size] = {0};

  index_entry() = default;
  index_entry(bool is_commit, int offset);

  bool is_commit() const;
  int num() const;
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
    int bitmap_byte_offset = 0;
    int bitmap_bit_offset = 0;
    unsigned char bitmap_byte = 0;

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

  int num_bits_so_far() const;
  int advance();
};
} // end namespace

int stream_gimmick::init_stream(int fd) {
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
int stream_gimmick::init_mmap(int fd) {
  assert(!is_initialized);
  is_initialized = true;
  is_stream = false;
  mmapped.init(fd);
  num_bytes = mmapped.num_bytes;
  return 0;
}
int stream_gimmick::seek_end() {
  assert(is_initialized);
  if (is_stream)
    return fseek(stream, 0, SEEK_END);
  position = num_bytes;
  return 0;
}
long stream_gimmick::tell() {
  assert(is_initialized);
  if (is_stream)
    return ftell(stream);
  return position;
}
static void limit_position(long &pos, int &count, size_t &num_bytes) {}
int stream_gimmick::seek_and_read(long pos, unsigned char *bytes, int count) {
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
int stream_gimmick::seek(long pos) {
  assert(is_initialized);
  if (is_stream)
    return fseek(stream, pos, SEEK_SET);
  if (pos > num_bytes)
    return 1;
  position = pos;
  return 0;
}
int stream_gimmick::read(unsigned char *bytes, int count) {
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
int stream_gimmick::write(const unsigned char *bytes, int count) {
  assert(is_initialized);
  assert(is_stream);
  return fwrite(bytes, 1, count, stream);
}

int stream_gimmick::close() {
  if (!is_initialized)
    return 0;
  is_initialized = false;
  if (is_stream)
    return fclose(stream);
  return mmapped.close();
}

int split2monodb::close_files() { return commits.close() | index.close(); }

bool index_entry::is_commit() const { return bytes[0] >> 7; }

int index_entry::num() const {
  unsigned data = 0;
  data |= bytes[0] << 16;
  data |= bytes[1] << 8;
  data |= bytes[2];
  return data & ((1ull << 23) - 1);
}

index_entry::index_entry(bool is_commit, int num) {
  assert(num >= 0);
  assert(num < (1 << 23));
  bytes[0] = static_cast<int>(is_commit) << 7 | num >> 16;
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

static int get_bitmap_bit(unsigned byte, int bit_offset) {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  return byte & (0x100 >> (bit_offset + 1)) ? 1 : 0;
}

static void set_bitmap_bit(unsigned char *byte, int bit_offset) {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  *byte |= 0x100 >> (bit_offset + 1);
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
        parse_number(ue.num_commits) ||
        parse_space(/*needs_any=*/true, /*newlines=*/false) ||
        parse_number(ue.num_upstreams) ||
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
  if (const char *verbose = getenv("VERBOSE"))
    if (strcmp(verbose, "0"))
      is_verbose = true;
  auto &db = *this;
  dbfd = open(dbdir, O_RDONLY);
  if (dbfd == -1)
    return error("could not open <dbdir>");
  int flags = db.is_read_only ? O_RDONLY : (O_RDWR | O_CREAT);
  int commitsfd = openat(dbfd, "commits", flags);
  int indexfd = openat(dbfd, "index", flags);
  int upstreamsfd = openat(dbfd, "upstreams", flags);
  int has_error = 0;
  if (commitsfd == -1)
    has_error |= db.is_read_only ? 1 : error("could not open <dbdir>/commits");
  if (indexfd == -1)
    has_error |= db.is_read_only ? 1 : error("could not open <dbdir>/index");
  if (upstreamsfd == -1)
    has_error |=
        db.is_read_only ? 1 : error("could not open <dbdir>/upstreams");
  if (has_error) {
    close(indexfd);
    close(commitsfd);
    close(upstreamsfd);
    return 1;
  }
  if (!db.is_read_only) {
    fchmod(indexfd, 0644);
    fchmod(commitsfd, 0644);
    fchmod(upstreamsfd, 0644);
  }

  if (db.commits.init(commitsfd, db.is_read_only))
    return error("could not open <dbdir>/commits");
  if (db.index.init(indexfd, db.is_read_only))
    return error("could not open <dbdir>/index");

  // Check that file sizes make sense.
  const unsigned char commits_magic[] = {'s', 2, 'm', 0xc, 0x0, 'm', 't', 's'};
  const unsigned char index_magic[] = {'s', 2, 'm', 0x1, 'n', 0xd, 0xe, 'x'};
  assert(sizeof(commits_magic) == magic_size);
  assert(sizeof(index_magic) == magic_size);
  if (db.commits.get_num_bytes()) {
    if (!db.index.get_num_bytes())
      return error("unexpected commits without index");
    if (db.commits.get_num_bytes() < magic_size ||
        (db.commits.get_num_bytes() - commit_pairs_offset) % commit_pair_size)
      return error("invalid commits");

    unsigned char magic[magic_size];
    if (db.commits.seek(0) ||
        db.commits.read(magic, magic_size) != magic_size ||
        memcmp(magic, commits_magic, magic_size))
      return error("bad commits magic");
  } else if (!db.is_read_only) {
    if (db.commits.seek(0) ||
        db.commits.write(commits_magic, magic_size) != magic_size)
      return error("could not write commits magic");
  }
  if (db.index.get_num_bytes()) {
    if (!db.commits.get_num_bytes())
      return error("unexpected index without commits");
    if (db.index.get_num_bytes() < magic_size)
      return error("invalid index");

    unsigned char magic[magic_size];
    if (db.index.seek(0) || db.index.read(magic, magic_size) != magic_size ||
        memcmp(magic, index_magic, magic_size))
      return error("bad index magic");
  } else if (!db.is_read_only) {
    if (db.index.seek(0) ||
        db.index.write(index_magic, magic_size) != magic_size)
      return error("could not write index magic");
  }
  this->upstreamsfd = upstreamsfd;
  return 0;
}

static int lookup_index_entry(split2monodb &db, index_query &q) {
  q.out.found = false;
  unsigned i = q.in.sha1.get_bits(q.in.start_bit, q.in.num_bits);
  q.out.entry_offset = q.in.entries_offset + i * index_entry_size;
  q.out.bitmap_byte_offset = q.in.bitmap_offset + i / 8;
  q.out.bitmap_bit_offset = i % 8;
  q.out.bitmap_byte = 0;

  // Not found.  Be resilient to an unwritten bitmap.
  if (db.index.seek_and_read(q.out.bitmap_byte_offset, &q.out.bitmap_byte, 1) !=
          1 ||
      !get_bitmap_bit(q.out.bitmap_byte, q.out.bitmap_bit_offset))
    return 0;

  q.out.found = true;
  if (db.index.seek_and_read(q.out.entry_offset, q.out.entry.bytes,
                             index_entry_size) != index_entry_size)
    return 1;
  return 0;
}

static int lookup_index_entry_from_root(split2monodb &db, index_query &q) {
  if (lookup_index_entry(db, q))
    return 1;
  if (!q.out.found)
    return 0;
  while (!q.out.entry.is_commit()) {
    q.advance();
    if (lookup_index_entry(db, q))
      return 1;
    if (!q.out.found)
      return 0;
  }
  return 0;
}

static int lookup_commit_bin_impl(split2monodb &db, index_query &q,
                                  bool &found_commit, binary_sha1 &found_sha1,
                                  int *commit_pair_offset) {
  if (lookup_index_entry_from_root(db, q))
    return 1;
  if (!q.out.found)
    return 0;

  // Look it up in the commits list.
  *commit_pair_offset = 0;
  int i = q.out.entry.num();
  *commit_pair_offset = commit_pairs_offset + commit_pair_size * i;
  if (db.commits.seek_and_read(*commit_pair_offset, found_sha1.bytes, 20) != 20)
    return 1;
  if (q.in.sha1 == found_sha1)
    found_commit = true;
  return 0;
}

static int lookup_commit(split2monodb &db, const textual_sha1 &split,
                         textual_sha1 &mono) {
  index_query q(split);

  bool found_commit = false;
  int commit_pair_offset = 0;
  binary_sha1 found_binsplit;
  if (lookup_commit_bin_impl(db, q, found_commit, found_binsplit,
                             &commit_pair_offset))
    return error("problem looking up split commit");
  if (!found_commit)
    return 1;
  binary_sha1 binmono;
  if (db.commits.seek_and_read(commit_pair_offset + 20, binmono.bytes, 20) !=
      20)
    return error("could not extract mono commit");
  mono = textual_sha1(binmono);
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

  textual_sha1 mono;
  if (lookup_commit(db, split, mono))
    return 1;
  // TODO: add a test for the exit status.
  return printf("%s\n", mono.bytes) != 41;
}

static int insert_one_bin_impl(split2monodb &db,
                               // FIXME: q should be const, but for bitmap_byte.
                               index_query &q, const binary_sha1 &binmono,
                               const binary_sha1 &found_binsplit,
                               int commit_pair_offset) {
  const auto &binsplit = q.in.sha1;
  bool need_new_subtrie = commit_pair_offset ? true : false;

  // add the commit to *commits*
  if (db.commits.seek_end())
    return error("could not seek in commits");
  int new_commit_pair_offset = db.commits.tell();
  int new_commit_num =
      (new_commit_pair_offset - commit_pairs_offset) / commit_pair_size;
  assert((new_commit_pair_offset - commit_pairs_offset) % commit_pair_size ==
         0);
  if (db.commits.write(binsplit.bytes, 20) != 20 ||
      db.commits.write(binmono.bytes, 20) != 20)
    return error("could not write commits");

  if (!need_new_subtrie) {
    // update the existing trie/subtrie
    index_entry entry(/*is_commit=*/true, new_commit_num);
    if (db.index.seek(q.out.entry_offset) ||
        db.index.write(entry.bytes, index_entry_size) != index_entry_size)
      return error("could not write index entry");

    // update the bitmap
    set_bitmap_bit(&q.out.bitmap_byte, q.out.bitmap_bit_offset);
    if (db.index.seek(q.out.bitmap_byte_offset) ||
        db.index.write(&q.out.bitmap_byte, 1) != 1)
      return error("could not update index bitmap");
    return 0;
  }

  // add subtrie(s) with full contents so far
  // TODO: add test that covers this.
  int first_mismatched_bit = binsplit.get_mismatched_bit(found_binsplit);
  assert(first_mismatched_bit < 160);
  int num_bits_so_far = q.num_bits_so_far();
  assert(first_mismatched_bit >= num_bits_so_far - num_subtrie_bits);

  // Make new subtries.
  struct trie_update_stack {
    int skip_bitmap_update;
    int bitmap_byte_offset;
    unsigned char bitmap_byte;

    int entry_offset;
    int is_commit;
    int num;
  };

  if (db.index.seek_end())
    return error("could not seek to end to discover num subtries");
  int end_offset = db.index.tell();
  int next_subtrie =
      end_offset <= subtrie_indexes_offset
          ? 0
          : 1 + (end_offset - subtrie_indexes_offset - 1) / subtrie_index_size;

  // Update index in reverse, so that if this gets aborted early (or killed)
  // the output file has no semantic changes.
  trie_update_stack stack[160 / num_subtrie_bits + 2] = {0};
  trie_update_stack *top = stack;

  // Start with updating the existing trie that is pointing at the conflicting
  // commit.  Note that the bitmap is already set, we just need to make it
  // point at the right place.
  top->skip_bitmap_update = 1;
  top->entry_offset = q.out.entry_offset;
  top->is_commit = 0;
  top->num = next_subtrie++;

  // Add some variables that need to last past the while loop.
  int commit_num =
      (commit_pair_offset - commit_pairs_offset) / commit_pair_size;
  int subtrie_offset, bitmap_offset;
  int n, n_entry_offset;
  int f, f_entry_offset;
  assert((commit_pair_offset - commit_pairs_offset) % commit_pair_size == 0);
  while (true) {
    // Calculate the entries for the next subtrie.
    subtrie_offset = subtrie_indexes_offset + top->num * subtrie_index_size;
    bitmap_offset = subtrie_offset + subtrie_index_bitmap_offset;
    n = binsplit.get_bits(num_bits_so_far, num_subtrie_bits);
    f = found_binsplit.get_bits(num_bits_so_far, num_subtrie_bits);
    n_entry_offset =
        subtrie_offset + subtrie_index_entries_offset + n * index_entry_size;
    f_entry_offset =
        subtrie_offset + subtrie_index_entries_offset + f * index_entry_size;

    if (n != f)
      break;

    // push another subtrie.
    num_bits_so_far += num_subtrie_bits;

    ++top;
    top->is_commit = 0;
    top->num = next_subtrie++;
    top->entry_offset = n_entry_offset;
    top->skip_bitmap_update = 0;
    top->bitmap_byte_offset = bitmap_offset + n / 8;
    set_bitmap_bit(&top->bitmap_byte, n % 8);
    assert(top->bitmap_byte);
  }

  // found a difference.  add commit entries to last subtrie.
  int fbyte_offset = bitmap_offset + f / 8;
  int nbyte_offset = bitmap_offset + n / 8;
  ++top;
  top->is_commit = 1;
  top->num = commit_num;
  top->entry_offset = f_entry_offset;
  top->skip_bitmap_update = 0;
  top->bitmap_byte_offset = fbyte_offset;
  set_bitmap_bit(&top->bitmap_byte, f % 8);
  assert(top->bitmap_byte);

  ++top;
  top->is_commit = 1;
  top->num = new_commit_num;
  top->entry_offset = n_entry_offset;
  top->skip_bitmap_update = nbyte_offset == fbyte_offset;
  top->bitmap_byte_offset = nbyte_offset;
  set_bitmap_bit(&top->bitmap_byte, n % 8);
  assert(top->bitmap_byte);
  if (top->skip_bitmap_update)
    top[-1].bitmap_byte |= top->bitmap_byte;

  // Unwind the stack.  Be careful not to decrement top past the beginning.
  ++top;
  while (top != stack) {
    --top;

    // Update the index entry.
    index_entry entry(top->is_commit, top->num);
    if (db.index.seek(top->entry_offset) ||
        db.index.write(entry.bytes, index_entry_size) != index_entry_size)
      return error("could not write index entry");

    if (top->skip_bitmap_update)
      continue;

    // Update the bitmap to point at the index entry.
    if (db.index.seek(top->bitmap_byte_offset) ||
        db.index.write(&top->bitmap_byte, 1) != 1)
      return error("could not write to index bitmap");
  }

  return 0;
}

static int insert_one(split2monodb &db, const unsigned char *binsplit,
                      const unsigned char *binmono) {
  binary_sha1 found_split, mono;
  auto q = index_query::from_binary(binsplit);
  mono.from_binary(binmono);
  int commit_pair_offset = 0;
  bool found_commit = false;
  if (lookup_commit_bin_impl(db, q, found_commit, found_split,
                             &commit_pair_offset))
    return error("index issue");
  assert(q.out.entry_offset);

  if (found_commit)
    return error("split is already mapped");

  return insert_one_bin_impl(db, q, mono, found_split, commit_pair_offset);
}

static int insert_one(split2monodb &db, const textual_sha1 &split,
                      const textual_sha1 &mono) {
  index_query q(split);
  binary_sha1 found_binsplit;
  int commit_pair_offset = 0;
  bool found_commit = false;
  if (lookup_commit_bin_impl(db, q, found_commit, found_binsplit,
                             &commit_pair_offset))
    return error("index issue");
  assert(q.out.entry_offset);

  if (found_commit)
    return error("split is already mapped");

  return insert_one_bin_impl(db, q, binary_sha1(mono), found_binsplit,
                             commit_pair_offset);
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

  return insert_one(db, split, mono);
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
    if (insert_one(db, split, mono))
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
    ue.num_commits = ue.num_upstreams = 0;
    existing_entry = main.upstreams.emplace(upstream.name, std::move(ue)).first;
  }

  if (existing_entry->second.num_commits > upstream.num_commits())
    return error("upstream is missing commits we already merged");

  if (existing_entry->second.num_upstreams > upstream.upstreams.size())
    return error("upstream is missing upstreams we already merged");

  // Nothing to do if nothing has changed (or the upstream is empty).
  if (existing_entry->second.num_commits == upstream.num_commits() &&
      existing_entry->second.num_upstreams == upstream.upstreams.size())
    return 0;

  // Merge the upstream's upstreams (just in memory, for now).
  for (auto ue : upstream.upstreams) {
    if (ue.second.name == main.name)
      return error("upstream: refusing to create upstream-cycle between dbs");
    auto &existing_ue = main.upstreams[ue.second.name];

    // Check that we're only moving forward.
    if (!existing_ue.name.empty())
      if (existing_ue.num_commits > ue.second.num_commits ||
          existing_ue.num_upstreams > ue.second.num_upstreams)
        return error("upstream's upstream is out-of-date");

    // Update.
    existing_ue = ue.second;
  }

  // Read all missing commits and merge them.
  long first_offset = commit_pairs_offset +
                      commit_pair_size * existing_entry->second.num_commits;
  long num_bytes = commit_pair_size * (upstream.num_commits() -
                                       existing_entry->second.num_commits);
  if (num_bytes) {
    // FIXME: This copy is dumb.  We should be using mmap for the upstream
    // commits, not FILE streams.
    std::vector<unsigned char> bytes;
    bytes.resize(num_bytes, 0);
    if (upstream.commits.seek_and_read(first_offset, bytes.data(), num_bytes) !=
        num_bytes)
      return error("could not read new commits from upstream");

    for (const unsigned char *b = bytes.data(),
                             *be = bytes.data() + bytes.size();
         b != be; b += commit_pair_size)
      if (insert_one(main, b, b + commit_pair_size / 2))
        return error("error inserting new commit from upstream");
  }

  // Close the streams.
  if (main.close_files())
    return error("error closing commits or index after writing");

  // Merge upstreams.
  existing_entry->second.num_commits = upstream.num_commits();
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
    if (fprintf(ufile, "upstream: %s %ld %ld\n", ue.second.name.c_str(),
                ue.second.num_commits, ue.second.num_upstreams) < 0)
      return error("could not write upstream");
  if (fclose(ufile))
    return error("problem closing new upstream");
  return 0;
}

static int dump_index(split2monodb &db, int num) {
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
  if (db.index.seek_and_read(bitmap_offset, bitmap, bitmap_size_in_bits / 8) !=
      bitmap_size_in_bits / 8)
    return 1;
  if (num == -1)
    printf("index num=root num-bits=%02d\n", num_bits);
  else
    printf("index num=%04d num-bits=%02d\n", num, num_bits);
  int any = 0;
  for (int i = 0, ie = bitmap_size_in_bits / 8; i != ie; ++i) {
    if (!bitmap[i])
      continue;
    for (int bit = 0; bit != 8; ++bit) {
      if (!get_bitmap_bit(bitmap[i], bit))
        continue;

      any = 1;
      int entry_i = i * 8 + bit;
      int offset = entries_offset + index_entry_size * entry_i;
      index_entry entry;
      if (db.index.seek_and_read(offset, entry.bytes, index_entry_size) !=
          index_entry_size)
        return 1;

      char bits[num_root_bits + 1] = {0};
      for (int i = 0; i < num_bits; ++i)
        bits[i] = entry_i & ((1u << (num_bits - i)) >> 1) ? '1' : '0';

      int entry_num = entry.num();
      if (entry.is_commit())
        printf("  entry: bits=%s commit=%08d\n", bits, entry_num);
      else
        printf("  entry: bits=%s  index=%04d\n", bits, entry_num);
    }
  }
  if (!any)
    error("no bits set in index...");
  return 0;
}

static int main_dump(const char *cmd, int argc, const char *argv[]) {
  if (argc != 1)
    return usage("dump: extra positional arguments", cmd);
  split2monodb db;
  db.is_read_only = true;
  if (db.opendb(argv[0]))
    return usage("could not open <dbdir>", cmd);

  unsigned char binsplit[21] = {0};
  unsigned char binmono[21] = {0};
  char split[41] = {0};
  char mono[41] = {0};
  if (db.commits.seek(commit_pairs_offset))
    return error("could not read any commit pairs");
  int i = 0;
  while (db.commits.seek_and_read(commit_pairs_offset + i * 40, binsplit, 20) ==
             20 &&
         db.commits.seek_and_read(commit_pairs_offset + i * 40 + 20, binmono,
                                  20) == 20) {
    bintosha1(split, binsplit);
    bintosha1(mono, binmono);
    printf("commit num=%08d split=%s mono=%s\n", i++, split, mono);
  }
  i = -1;
  while (!dump_index(db, i))
    ++i;
  return 0;
}

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing command", argv[0]);
#define SUBMAIN(X)                                                             \
  do {                                                                         \
    if (!strcmp(argv[1], #X))                                                  \
      return main_##X(argv[0], argc - 2, argv + 2);                            \
  } while (false)
  SUBMAIN(create);
  SUBMAIN(lookup);
  SUBMAIN(insert);
  SUBMAIN(upstream);
  SUBMAIN(dump);
#undef SUBMAIN
  return usage("unknown command", argv[0]);
}
