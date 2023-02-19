#pragma once

extern "C" {
  #include "libvfio-user.h"
}

class Capabilities {
  public:
    vfu_ctx_t *vfu_ctx_stub;

    Capabilities(const vfio_region_info *config_info, void *config_ptr);

    void *msix(size_t *cap_size);

  private:
    void *header_mmap;
    size_t header_size;

    void map_header();
};
