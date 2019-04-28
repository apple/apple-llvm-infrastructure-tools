// index_query.h
#pragma once

#include "file_stream.h"

namespace {
// Some constants.
static constexpr const long magic_size = 8;
static constexpr const long num_root_bits = 14;
static constexpr const long num_subtrie_bits = 6;
static constexpr const long root_index_bitmap_offset = magic_size;

static_assert(sizeof(binary_sha1) == 20);

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
} // end namespace

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
