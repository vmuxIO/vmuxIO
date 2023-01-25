#pragma once
#include<vector>
#include <map>

/**
 * Endpoint towards kernel
 */
class VfioConsumer {
  public:
    std::vector<struct vfio_region_info> regions;
    std::vector<struct vfio_irq_info> interrupts;
    std::map<int, void*> mmio;

    // vfio device fd TODO close
    int device;

    void* registers;

    int init();
    int init_mmio();
};

#define VFIOC_SECRET 1337
