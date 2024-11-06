#define _GNU_SOURCE
#include "memfd.hpp"

#include <unistd.h>
#include <sys/mman.h>
#include "util.hpp"

MemFd::MemFd(const char *name, size_t size) : _size(size) {
  _fd = memfd_create(name, MFD_CLOEXEC);
  if (_fd == -1) {
    die("memfd_create failed: %s", strerror(errno));
  }

  if (ftruncate(_fd, size) == -1) {
    die("memfd ftruncate failed: %s", strerror(errno));
  }

  _ptr = (unsigned char *)mmap(NULL, size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, _fd, 0);
  if (_ptr == MAP_FAILED) {
    die("memfd mmap failed: %s", strerror(errno));
  }
}

MemFd::~MemFd() {
  munmap(_ptr, _size);
  close(_fd);
}
