#pragma once
#include <cstddef>

// Abstract class for Driver backends
class Driver {
public:
  static const int MAX_BUF = 9000; // should be enough even for most jumboframes

  int fd = 0; // may be a non-null fd to poll on
  char rxFrame[MAX_BUF];
  size_t rxFrame_used; // how much rxFrame is actually filled with data
  char txFrame[MAX_BUF];

  virtual void send(const char *buf, const size_t len) = 0;
  virtual void recv() = 0;
};
