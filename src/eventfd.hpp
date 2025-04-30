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

class Epoll {
  int epoll_fd;

public:
  Epoll();
  ~Epoll();

  Epoll(const Epoll &) = delete;
  const Epoll &operator=(const Epoll &) = delete;

  void add(const EventFd &);
  void add(int fd);
  void wait(int timeout_ms = -1);

  int fd() const { return epoll_fd; }
};
