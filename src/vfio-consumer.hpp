#pragma once
#include<vector>
#include <map>
#include <linux/vfio.h>
extern "C" {
  #include "libvfio-user.h"
}

/**
 * Endpoint towards kernel
 */
class VfioConsumer {
  public:
    std::vector<struct vfio_region_info> regions;
    std::vector<struct vfio_irq_info> interrupts;
    std::vector<int> irqfds; // eventfds for MSIX interrupts
    int irqfd_intx; // more legacy interrupts
    int irqfd_msi;
    int irqfd_err;
    int irqfd_req;
    std::map<int, void*> mmio;
    struct vfio_iommu_type1_dma_map dma_map;



    // vfio fds
    int container;
    int group;
    int device;

    ~VfioConsumer();
    int init();
    int init_mmio();
    void init_msix();
    void init_legacy_irqs();
    void reset_device();
    void map_dma(vfio_iommu_type1_dma_map *dma_map);
    void unmap_dma(vfio_iommu_type1_dma_unmap *dma_unmap);
    void mask_irqs(uint32_t irq_type, uint32_t start, uint32_t count, bool mask);
};

#define VFIOC_SECRET 1337
