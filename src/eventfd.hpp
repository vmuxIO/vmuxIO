#pragma once

class EventFd {
  int _fd;

public:
  EventFd();
  explicit EventFd(unsigned initval);
  ~EventFd();

  EventFd(const EventFd &) = delete;
  const EventFd &operator=(const EventFd &) = delete;

  int fd() const { return _fd; }

  void reset();

  void signal();
};

class EventFdWaiter {
  int epoll_fd;

public:
  EventFdWaiter();
  ~EventFdWaiter();

  EventFdWaiter(const EventFdWaiter &) = delete;
  const EventFdWaiter &operator=(const EventFdWaiter &) = delete;

  void add(const EventFd &);
  void wait(int timeout_ms = -1);
};
