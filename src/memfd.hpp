#pragma once

#include <cstddef>

class MemFd {
  int _fd;
  unsigned char *_ptr;
  size_t _size;

public:
  MemFd(const char *name, size_t size);
  ~MemFd();

  MemFd(const MemFd &) = delete;
  const MemFd &operator=(const MemFd &) = delete;

  int fd() const {
    return _fd;
  }

  unsigned char *ptr() const {
    return _ptr;
  }

  unsigned char &operator[](size_t n) {
    return _ptr[n];
  }

  size_t size() const {
    return _size;
  }
};
