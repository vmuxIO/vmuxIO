
#pragma once

#include <stdexcept>

// exit() and err() breaks invariants for RAII (destructors). Therefore we use
// warn() instead to printf an error and throw afterwards to exit.
#define die(...) { \
  warn(__VA_ARGS__); \
  throw std::runtime_error("See error above"); \
  }
