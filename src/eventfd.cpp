#include "src/eventfd.hpp"

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include "src/util.hpp"

EventFd::EventFd() : EventFd(0) {}

EventFd::EventFd(unsigned initval) {
  _fd = eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
  if (_fd == -1) {
    die("failed to create event fd");
  }
}

EventFd::~EventFd() {
  close(_fd);
}

void EventFd::reset() {
  uint64_t c;

  // Fails only if eventfd counter was already 0,
  // which is the outcome we want anyway
  read(_fd, &c, 8);
}

void EventFd::signal() {
  uint64_t c = 1;
  ssize_t ret = write(_fd, &c, 8);
  if (ret != 8) {
    // Can fail if the 64-bit counter would overflow,
    // which we can ignore.
    // Logging this case anyway, because it should be incredibly unlikely.
    printf("eventfd write failed\n");
  }
}

EventFdWaiter::EventFdWaiter() {
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd == -1) {
    die("epoll_create failed");
  }
}

EventFdWaiter::~EventFdWaiter() {
  close(epoll_fd);
}

void EventFdWaiter::add(const EventFd &eventfd) {
  struct epoll_event evt = {
    .events = EPOLLIN
  };
  int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, eventfd.fd(), &evt);
  if (ret != 0) {
    die("epoll_ctl failed");
  }
}

void EventFdWaiter::wait(int timeout_ms) {
  struct epoll_event dummy;
  int ret = epoll_wait(epoll_fd, &dummy, 1, timeout_ms);
  if (ret == -1 && errno != EINTR) {
    perror("epoll_wait");
  }
}
