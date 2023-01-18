#pragma once
#include<vector>

/**
 * Endpoint towards kernel
 */
class VfioConsumer {
  public:
    std::vector<struct vfio_region_info> regions;
    std::vector<struct vfio_irq_info> interrupts;

    int init();
};

#define VFIOC_SECRET 1337
