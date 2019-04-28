// file_stream.h
#pragma once

#include "mmapped_file.h"
#include <cstdio>
#include <cstring>

namespace {
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
} // end namespace

int file_stream::init_stream(int fd) {
  assert(fd != -1);
  assert(!is_initialized);
  if (!(stream = fdopen(fd, "w+b")) || fseek(stream, 0, SEEK_END) ||
      (num_bytes = ftell(stream)) == -1u || fseek(stream, 0, SEEK_SET)) {
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
int file_stream::seek_and_read(long pos, unsigned char *bytes, int count) {
  assert(is_initialized);
  // Check that the position is valid first.
  if (pos >= (long)num_bytes)
    return 0;
  if (pos > (long)num_bytes || seek(pos))
    return 0;
  if (count + pos > (long)num_bytes)
    count = num_bytes - pos;
  if (!count)
    return 0;
  return read(bytes, count);
}
int file_stream::seek(long pos) {
  assert(is_initialized);
  if (is_stream)
    return fseek(stream, pos, SEEK_SET);
  if (pos > (long)num_bytes)
    return 1;
  position = pos;
  return 0;
}
int file_stream::read(unsigned char *bytes, int count) {
  assert(is_initialized);
  if (is_stream)
    return fread(bytes, 1, count, stream);
  if (position + count > (long)num_bytes)
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
