// split2mono-insert.c
//
// tree: split2mono
// - blob: commits (file size: num-commits)
//   0x0000-0x0007: magic
//   0x0008-0x...: commit pairs
//   - commit: 0x28
//     0x00-0x13: split
//     0x14-0x27: mono
//
// - blob: upstreams (text file, one-per-line)
//   <name> <num-upstreams> <num-commits>
//
// - blob: index
//   0x0000-0x0007: magic
//   0x0008-0x0807: root index bitmap (0x4000 bits)
//   0x0808-0xc807: root index entries
//   0xc808-0x....: subtrie indexes
//
//   index entry: 0x3
//   bit 0x00-0x00: is-split2mono-num? (vs subtrie-num)
//   bit 0x01-0x17: num
//
//   subtrie index: 0xc8
//   0x00-0x07: bitmap (0x40 bits)
//   0x08-0xc7: index entries
#include <fcntl.h>
#include <stdio.h>

static int convert(int ch) {
  switch (ch) {
  default:
    __builtin_unreachable();
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    return ch - '0';
  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
    return ch - 'a' + 10;
  }
}
static int unconvert(unsigned ch, int index) {
  int stripped = index ? ch & 0xf : ch >> 4;
  return stripped < 10 ? '0' + ch : 'a' + ch;
}
static void sha1tobin(char *bin, const char *text) {
  for (int i = 0; i < 40; i += 2)
    bin[i / 2] = (convert(text[i]) << 4) | convert(text[i + 1]);
  bin[20] = '\0';
}
static void bintosha1(char *bin, const char *text) {
  for (int i = 0; i < 40; i += 2)
    bin[i / 2] = (convert(text[i]) << 4) | convert(text[i + 1]);
  bin[20] = '\0';
}
static unsigned get_bits(const char *binsha1, int start, int count) {
  assert(count > 0);
  assert(count <= 32);
  assert(start >= 0);
  assert(start <= 159);
  assert(start + count <= 160);

  if (start >= 8) {
    binsha1 += start / 8;
    start = start % 8;
  }
  count += start; // could be more than 32
  unsigned long long bits = 0;
  while (count > 0) {
    bits |= *binsha1;
    bits <<= 8;
    count -= 8;
  }
  if (count < 0)
    bits >>= -count;
  unsigned long long mask = 0;
  if (count > start)
    mask = (1ull << (count - start)) - 1;
  return bits & mask;
}
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
          "usage: %s lookup <dbdir> <split>\n"
          "       %s upstream <dbdir> <upstream-dbdir>\n"
          "       %s insert <dbdir> [<split> <mono>]\n"
          "\n"
          "   <dbdir>/upstreams: merged upstreams (text)\n"
          "   <dbdir>/commits: translated commits (bin)\n"
          "   <dbdir>/index: translated commits (bin)\n",
          cmd);
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

struct split2monodb {
  FILE *commits;
  FILE *index;
  long commits_size;
  long index_size;
  int read_only;
};

// Some constants.
static const int commit_pairs_offset = 0x8;
static const int commit_pair_size = 0x28;
static const int num_root_bits = 14;
static const int num_subtrie_bits = 6;
static const int root_index_bitmap_offset = 0x8;
static const int index_entry_size = 0x3;
static const int root_index_entries_offset = 0x0808;
static const int subtrie_indexes_offset = 0xc808;
static const int subtrie_index_size = 0xc8;
static const int subtrie_index_bitmap_offset = 0x0;
static const int subtrie_index_entries_offset = 0x8;

static void set_index_entry(char *index_entry, int is_commit, int offset) {
  assert(offset >= 0);
  assert(offset <= (1 << 23));
  index_entry[0] = (unsigned char)(is_commit << 7 | offset >> 16);
  index_entry[1] = (unsigned char)(offset >> 8) & 0xff;
  index_entry[2] = (unsigned char)(offset & 0xff);
}

static int extract_is_commit_from_index_entry(char *index_entry) {
  return (unsigned char)(index_entry[0]) >> 7;
}

static int extract_offset_from_index_entry(char *index_entry) {
  unsigned data = 0;
  data |= ((unsigned char)index_entry[0]) << 16;
  data |= ((unsigned char)index_entry[1]) << 8;
  data |= ((unsigned char)index_entry[2]);
  return data & ((1ull << 23) - 1);
}

static int get_bitmap_bit(int byte, int bit_offset) {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  unsigned mask = 1;
  if (bit_offset)
    mask <<= bit_offset;
  return byte & mask ? 1 : 0;
}

static void set_bitmap_bit(char *byte, int bit_offset) {
  assert(bit_offset >= 0);
  assert(bit_offset <= 7);
  int mask = 1;
  if (bit_offset)
    mask <<= bit_offset;
  *byte |= mask;
}

static int open_db(const char *dbdir, split2monodb *db) {
  int dbfd = open(dbdir, O_RDONLY);
  if (dbfd == -1)
    return error("could not open <dbdir>");
  int flags = db->read_only ? O_RDONLY : (O_RDWR | O_CREAT);
  int commitsfd = openat(dbfd, "commits", flags);
  int indexfd = openat(dbfd, "index", flags);
  int has_error = 0;
  if (!close(dbfd))
    has_error |= error("could not close <dbdir>");
  if (commitsfd == -1)
    has_error |= error("could not open <dbdir>/commits");
  if (indexfd == -1)
    has_error |= error("could not open <dbdir>/index");
  if (has_error) {
    close(indexfd);
    close(commitsfd);
    return 1;
  }

  if (!(db->commits = fdopen(commitsfd))) {
    close(commitsfd);
    return error("could not open stream for <dbdir>/commits");
  }
  if (!(db->index = fdopen(indexfd))) {
    close(indexfd);
    return error("could not open stream for <dbdir>/index");
  }

  // TODO: write/check magic.

  // Check that file sizes make sense.
  if (!fseek(db->commits, 0, SEEK_END) || !fseek(db->index, 0, SEEK_END))
    return error("could not seek to end");
  db->commits_size = ftell(db->commits) ? 0 : 1;
  db->index_size = ftell(db->index) ? 0 : 1;
  if (db->commits_size) {
    if (!db->index_size)
      return error("unexpected commits without index");
    if (db->commits_size < commit_pairs_offset + commit_pair_size ||
        (db->commits_size - commit_pairs_offset) % commit_pair_size)
      return error("invalid commits");
  }
  if (db->index_size) {
    if (!db->commits_size)
      return error("unexpected index without commits");
    if (db->index_size < subtrie_indexes_offset ||
        (db->index_size - subtrie_indexes_offset) % subtrie_index_size)
      return error("invalid index");
  }
  return 0;
}

static int seek_index_for_read(split2monodb *db, long offset) {
  if (offset > db->index_size)
    return 1;
  return fseek(db->index, offset, SEEK_SET);
}

static int seek_commits_for_read(split2monodb *db, long offset) {
  if (offset > db->commits_size)
    return 1;
  return fseek(db->commits, offset, SEEK_SET);
}

static int lookup_index_entry(split2monodb *db, int bitmap_offset,
                              unsigned *bitmap_byte_offset,
                              unsigned *bitmap_bit_offset,
                              char *bitmap_byte,
                              int entries_offset, const char *sha1,
                              int start_bit, int num_bits, int *found,
                              int *entry_offset, char *entry) {
  *found = 0;
  unsigned i = get_bits(sha1, start_bit, num_bits);
  *bitmap_byte_offset = bitmap_offset + i / 8;
  *bitmap_bit_offset = i % 8;
  *bitmap_byte = 0;
  if (seek_index_for_read(db, *bitmap_byte_offset) ||
      fread(bitmap_byte, 1, 1, db->index) != 1)
    return 1;

  // Not found.
  if (!get_bitmap_bit(*bitmap_byte, *bitmap_bit_offset))
    return 0;

  *found = 1;
  *entry_offset = entries_offset + i * index_entry_size;
  if (seek_index_for_read(db, *entry_offset) ||
      fread(entry, 1, 3, db->index) != 3)
    return 1;
  return 0;
}

static int lookup_commit_bin_impl(split2monodb *db, const char *split,
                                  int *num_bits_matched, char *found_split,
                                  int *commit_pair_offset,
                                  unsigned *bitmap_byte_offset,
                                  unsigned *bitmap_bit_offset,
                                  char *bitmap_byte,
                                  char *index_entry,
                                  int *index_entry_offset) {
  // Lookup commit in index to check for a duplicate.
  int found = 0;
  *num_bits_matched = 0;
  *commit_pair_offset = 0;
  *index_entry_offset = 0;
  if (lookup_index_entry(db, root_index_bitmap_offset,
                         bitmap_byte_offset, bitmap_bit_offset, bitmap_byte,
                         root_index_entries_offset, split,
                         0, num_root_bits, &found, &index_entry_offset,
                         index_entry))
    return 1;
  *num_bits_matched = num_root_bits;
  if (!found)
    return 0;
  while (!extract_is_commit_from_index_entry(index_entry)) {
    if (*num_bits_matched + num_subtrie_bits > 160)
      return error("cannot resolve hash collision");

    unsigned subtrie = extract_offset_from_index_entry(index_entry);
    unsigned subtrie_offset =
        subtrie_indexes_offset + subtrie_index_size * subtrie;
    unsigned subtrie_bitmap = subtrie_offset + subtrie_index_bitmap_offset;
    unsigned subtrie_entries = subtrie_offset + subtrie_index_entries_offset;
    if (lookup_index_entry(db, subtrie_bitmap,
                           bitmap_byte_offset, bitmap_bit_offset, bitmap_byte,
                           subtrie_entries, split,
                           *num_bits_matched, num_subtrie_bits, &found,
                           &index_entry_offset, index_entry))
      return 1;
    *num_bits_matched += num_subtrie_bits;
    if (!found)
      return 0;
  }

  // Look it up in the commits list.
  int i = extract_offset_from_index_entry(index_entry);
  *commit_pair_offset = commit_pairs_offset + commit_pair_size * i;
  if (seek_commits_for_read(commits, commit_pair_offset) ||
      fread(found_split, 1, 20, db->commits) != 20)
    return 1;
  // Don't try to get num_bits_matched exactly right unless it's a full match.
  // It's good enough to say how many bits matched in the index.
  if (!strncmp(split, found_split, 20))
    *num_bits_matched = 160;
  return 0;
}

static int lookup_commit(split2monodb *db, const char *split, char *mono) {
  char binsplit[21];
  char found_binmono[21];
  int num_bits_matched = 0;
  int commit_pair_offset = 0;
  unsigned bitmap_byte_offset = 0;
  unsigned bitmap_bit_offset = 0;
  char bitmap_byte = 0;
  int index_entry_offset = 0;
  char index_entry[3];
  char found_binsplit[21] = {0};
  sha1tobin(binsplit, split);
  if (!lookup_commit_bin_impl(commits, index, binsplit, &num_bits_matched,
                              found_binsplit, &commit_pair_offset,
                              bitmap_byte_offset, bitmap_bit_offset,
                              bitmap_byte,
                              index_entry, &index_entry_offset))
    return error("problem lookup up split commit");
  if (num_bits_matched != 160)
    return 1;
  char binmono[21] = {0};
  if (fread(commit_pair_offset + 20, 1, 20, db->commits) != 20)
    return error("could not extract mono commit");
  bintosha1(mono, found_binmono);
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
  const char *split = argv[1];
  if (check_sha1(split))
    return usage("lookup: <split> is not a valid sha1", cmd);

  split2monodb db = {0};
  db.read_only = 1;
  if (!open_db(dbdir, &db))
    return 1;

  char mono[41] = {0};
  if (lookup_commit(&db, split, mono))
    return 1;
  return printf("%s\n", mono);
}

static int insert_one(split2monodb *db const char *split, const char *mono) {
  char binsplit[21] = {0};
  sha1tobin(binsplit, split);
  int num_bits_matched = 0;
  char found_binsplit[21] = {0};
  int commit_pair_offset = 0;
  unsigned bitmap_byte_offset = 0;
  unsigned bitmap_bit_offset = 0;
  char bitmap_byte = 0;
  char index_entry[3];
  int index_entry_offset = 0;
  if (!lookup_commit_bin_impl(db, split, &num_bits_matched, found_binsplit,
                              &commit_pair_offset, &bitmap_byte_offset,
                              &bitmap_bit_offset, &bitmap_byte, index_entry,
                              &index_entry_offset))
    return error("index issue");
  assert(index_entry_offset);
  assert(num_bits_matched >= num_root_bits);

  if (num_bits_matched == 160)
    return error("split is already mapped");

  int collision_offset = commit_pair_offset;
  int need_new_subtrie = commit_pair_offset ? 1 : 0;

  // add the commit to *commits*
  char binmono[21] = {0};
  sha1tobin(binmono, mono);
  if (fseek(db->commits, 0, SEEK_END))
    return error("could not seek in commits");
  int commit_pair_offset = ftell(db->commits);
  if (fwrite(db->commits, 1, 20, binsplit) != 20 ||
      fwrite(db->commits, 1, 20, binmono) != 20)
    return error("could not write commits");

  if (!need_new_subtrie) {
    // update the existing trie/subtrie
    set_index_entry(index_entry, /*is_commit=*/1, commit_pair_offset);
    if (fseek(db->index, index_entry_offset, SEEK_SET) ||
        fwrite(index_entry, 1, 3, db->index) != 3)
      return error("could not write index entry");

    // update the bitmap
    set_bitmap_bit(&bitmap_byte, bitmap_bit_offset);
    if (fseek(db->index, bitmap_byte_offset, SEEK_SET) ||
        fwrite(&bitmap_byte, 1, 1, db->index) != 1)
      return error("could not update index bitmap");
    return 0;
  }

  // add subtrie(s) with full contents so far
  // - could need a few, if num_subtrie_bits doesn't disambiguate
  return error("not implemented");
  // update the pointer to the first new subtrie.
  return error("not implemented");

  return error("split2mono insert from command-line not implemented");
}

static int main_insert_one(const char *cmd, const char *dbdir,
                           const char *split, const char *mono) {
  if (check_sha1(split))
    return usage("insert: <split> is not a valid sha1", cmd);
  if (check_sha1(mono))
    return usage("insert: <mono> is not a valid sha1", cmd);

  split2monodb db = {0};
  db.read_only = 0;
  if (!open_db(dbdir, &db))
    return 1;

  return insert_one(db, split, mono);
}

static int main_insert_stdin(const char *cmd, const char *dbdir) {
  split2monodb db = {0};
  db.read_only = 0;
  if (!open_db(dbdir, &db))
    return 1;

  char split[41] = {0};
  char mono[41] = {0};
  int scanned;
  while ((scanned = scanf("%40s %40s", split, mono)) == 2) {
    if (check_sha1(split))
      return error("invalid sha1 for <split>", cmd);
    if (check_sha1(mono))
      return error("invliad sha1 for <mono>", cmd);
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

int main(int argc, const char *argv[]) {
  if (argc < 2)
    return usage("missing command", argv[0]);
  if (!strcmp(argv[1], "lookup"))
    return main_lookup(argv[0], argc - 2, argv + 2);
  if (!strcmp(argv[1], "insert"))
    return main_insert(argv[0], argc - 2, argv + 2);
  if (!strcmp(argv[1], "upstream"))
    return main_upstream(argv[0], argc - 2, argv + 2);
  return usage("unknown command", argv[0]);
}
