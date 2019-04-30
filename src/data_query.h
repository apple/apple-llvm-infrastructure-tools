// data_query.h
#pragma once

#include "file_stream.h"
#include "index_query.h"
#include "svnbaserev.h"

namespace {
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

int table_streams::close_files() {
  // Report both errors but close both.
  int failed = 0;
  if (data.close())
    failed |= error("failed to close " + name + " data: " + strerror(errno));
  if (index.close())
    failed |= error("failed to close " + name + " index: " + strerror(errno));
  return failed;
}

int table_streams::init(int dbfd, bool is_read_only, const unsigned char *magic,
                        int record_offset, int record_size) {
  int flags = is_read_only ? O_RDONLY : (O_RDWR | O_CREAT);
  std::string index_name = name + ".index";
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
  if (data.get_num_bytes_on_open()) {
    if (!index.get_num_bytes_on_open())
      return error("unexpected data without index for " + name);
    if (data.get_num_bytes_on_open() < magic_size ||
        (data.get_num_bytes_on_open() - record_offset) % record_size)
      return error("invalid data for " + name);

    unsigned char file_magic[magic_size];
    if (data.seek(0) || data.read(file_magic, magic_size) != magic_size ||
        memcmp(file_magic, magic, magic_size))
      return error("bad magic for " + name);
  } else if (!is_read_only) {
    if (data.seek(0) || data.write(magic, magic_size) != magic_size)
      return error("could not write magic for " + name);
  }
  if (index.get_num_bytes_on_open()) {
    if (!data.get_num_bytes_on_open())
      return error("unexpected index without " + name);
    if (index.get_num_bytes_on_open() < magic_size)
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

template <class T> int data_query<T>::lookup_data_impl(table_streams &ts) {
  if (lookup(ts.index))
    return 1;
  if (!out.found)
    return 0;

  // Look it up in the data table.
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
    return error("problem looking up " + std::string(T::key_name) + " key");
  if (!found_data)
    return 1;
  if (ts.data.seek_and_read(data_offset + 20, value.bytes, T::value_size) !=
      T::value_size)
    return error("could not extract " + std::string(T::key_name) +
                 " after finding " + T::value_name);
  return 0;
}

template <class T>
int data_query<T>::insert_data_impl(table_streams &ts,
                                    const value_type &value) {
  bool need_new_subtrie = data_offset ? true : false;

  // Add value to the data file.
  if (ts.data.seek_end())
    return error("could not seek in " + std::string(T::table_name) + " table");
  int new_data_offset = ts.data.tell();
  int new_num = (new_data_offset - table_type::table_offset) / table_type::size;
  assert((new_data_offset - table_type::table_offset) % table_type::size == 0);
  if (ts.data.write(in.sha1.bytes, 20) != 20 ||
      ts.data.write(value.bytes, table_type::value_size) !=
          table_type::value_size)
    return error("could not write " + std::string(T::value_name));

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

template <class T> static int dump_table(table_streams &ts) {
  typedef T table_type;
  typedef typename table_type::value_type value_type;

  // Print the table.
  if (ts.data.seek(table_type::table_offset))
    return error("could not read data from " + std::string(T::table_name) +
                 " table");
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
