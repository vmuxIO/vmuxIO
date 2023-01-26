#pragma once
#include<vector>
#include <map>
#include <linux/vfio.h>

/**
 * Endpoint towards kernel
 */
class VfioConsumer {
  public:
    std::vector<struct vfio_region_info> regions;
    std::vector<struct vfio_irq_info> interrupts;
    std::map<int, void*> mmio;
    struct vfio_iommu_type1_dma_map dma_map;


    // vfio fds
    int container;
    int group;
    int device;

    ~VfioConsumer();
    int init();
    int init_mmio();
};

#define VFIOC_SECRET 1337
