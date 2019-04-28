// bump_allocator.h

#pragma once

#include <cstdlib>
#include <memory>

namespace {
struct bump_allocator {
  struct slab_type {
    slab_type *next = nullptr;
    void *mem = nullptr;
  };

  int num_slabs = 0;
  slab_type *current_slab = nullptr;
  char *next_byte = nullptr;
  char *last_byte = nullptr;
  static constexpr const size_t slab_size = 4096 << 3;

  bump_allocator() = default;
  bump_allocator(const bump_allocator &) = delete;

  ~bump_allocator();

  template <typename T> void *allocate(size_t num = 1) {
    return allocate(num * sizeof(T), alignof(T));
  }

  void *allocate(size_t size, size_t alignment);
  void add_slab();
  static uintptr_t align(const void *x, size_t alignment);
};
} // end namespace

bump_allocator::~bump_allocator() {
  for (slab_type *slab = current_slab; slab;) {
    slab_type *next = slab->next;
    free(slab);
    slab = next;
  }
}

uintptr_t bump_allocator::align(const void *x, size_t alignment) {
  return ((uintptr_t)x + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

void *bump_allocator::allocate(size_t size, size_t alignment) {
  // this is not general purpose; lots of corner cases are NOT handled.
  assert(alignment <= 8);
  assert(size <= slab_size / 2);

  size_t adjust = align(next_byte, alignment) - (uintptr_t)next_byte;
  assert(adjust + size >= size);
  if (adjust + size <= size_t(last_byte - next_byte)) {
    char *aligned = next_byte + adjust;
    next_byte = aligned + size;
    return aligned;
  }
  add_slab();
  return allocate(size, alignment);
}

void bump_allocator::add_slab() {
  size_t new_slab_size = slab_size;
  if (unsigned scale = num_slabs++ / 128)
    new_slab_size *= 1 << (scale > 27 ? 27 : scale);
  void *mem = malloc(new_slab_size);
  auto *new_slab = new (mem) slab_type();
  new_slab->mem = mem;
  new_slab->next = current_slab;
  current_slab = new_slab;
  next_byte = (char *)mem + sizeof(slab_type);
  last_byte = (char *)mem + new_slab_size;
}

void *operator new(size_t size, bump_allocator &alloc) {
  struct S {
    char c;
    union {
      double D;
      long double LD;
      long long L;
      void *P;
    } x;
  };

  uint64_t align = size;
  align |= (align >> 1);
  align |= (align >> 2);
  align |= (align >> 4);
  align |= (align >> 8);
  align |= (align >> 16);
  align |= (align >> 32);
  ++align;
  if (align > offsetof(S, x))
    align = offsetof(S, x);

  return alloc.allocate(size, align);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
void *operator new[](size_t size, bump_allocator &alloc) {
  return operator new(size, alloc);
}
#pragma clang diagnostic pop
