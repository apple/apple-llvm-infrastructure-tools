// mmapped_file.h
#pragma once

#include <cassert>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct mmapped_file {
  long num_bytes = 0;
  const char *bytes = nullptr;

  mmapped_file() = default;
  explicit mmapped_file(const char *file) { init(file); }
  explicit mmapped_file(int fd) { init(fd); }
  int init(const char *file);
  int init(int fd);
  ~mmapped_file() { close(); }
  int close();
};
} // end namespace

int mmapped_file::init(const char *file) {
  int fd = open(file, O_RDONLY);
  if (fd == -1)
    return 1;
  return init(fd);
}
int mmapped_file::init(int fd) {
  assert(fd != -1);
  struct stat st;
  if (fstat(fd, &st))
    return 1;
  num_bytes = st.st_size;
  if (num_bytes) {
    bytes = static_cast<char *>(
        mmap(nullptr, num_bytes, PROT_READ, MAP_PRIVATE, fd, 0));
    if (bytes == MAP_FAILED)
      return 1;
  }
  ::close(fd);
  return 0;
}
int mmapped_file::close() {
  if (!bytes)
    return 0;
  return munmap(const_cast<char *>(bytes), num_bytes);
}
