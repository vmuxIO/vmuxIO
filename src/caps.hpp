/**
 * We keep header and implementation split here so not to pollute this project
 * with stuff defined in the private libvfio-user header imported in the
 * implementation.
 */

#pragma once

#include <string>

extern "C" {
#include "libvfio-user.h"
}

class Capabilities {
public:
  // Contains the pci config space of the passed-through device
  // (underlying device). Does not contain any vfio-user context. Used to
  // apply vfu functions to host config space.
  vfu_ctx_t *vfu_ctx_stub;

  Capabilities(const vfio_region_info *config_info, std::string device);
  ~Capabilities();

  void *capa(const char name[], int id, size_t size, bool extended);
  void *dsn();
  void *pm();
  void *msi();
  void *msix();
  void *exp();
  void *vpd();
  void *ptm();

private:
  // copy of config space of underlying device
  void *header_mmap = NULL;
  size_t header_size;

  void map_header(std::string device);
};
