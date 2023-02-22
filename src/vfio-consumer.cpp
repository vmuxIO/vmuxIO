#include "src/vfio-consumer.hpp"
#include <linux/vfio.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vector>

#include "src/util.hpp"

VfioConsumer::~VfioConsumer() {
  printf("vfio consumer destructor called\n");
  int ret;
  ret = close(this->container);
  if (ret < 0) {
    warn("Cleanup: Cannot close vfio conatiner");
  }
  ret = close(this->group);
  if (ret < 0) {
    warn("Cleanup: Cannot close vfio group");
  }
  ret = close(this->device);
  if (ret < 0) {
    warn("Cleanup: Cannot close vfio device");
  }
  ret = munmap((void*)dma_map.vaddr, dma_map.size);
  if (ret < 0) {
    warn("Cleanup: Cannot unmap dma");
  }
  for (auto const& [idx, ptr] : this->mmio) {
    ret = munmap(ptr, this->regions[idx].size);
    if (ret < 0) {
      warn("Cleanup: Cannot unmap BAR regions %d", idx);
    }
  }
}

int VfioConsumer::init() {
  int ret, container, group, device;
  uint32_t i;
  struct vfio_group_status group_status =
                                  { .argsz = sizeof(group_status) };
  struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(iommu_info) };
  struct vfio_iommu_type1_dma_map dma_map = { .argsz = sizeof(dma_map) };
  struct vfio_device_info device_info = { .argsz = sizeof(device_info) };

  /* Create a new container */
  container = open("/dev/vfio/vfio", O_RDWR);
  if (container < 0) {
    die("Cannot open /dev/vfio/vfio");
  }
  this->container = container;

  if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
    /* Unknown API version */
    die("VFIO version mismatch");
  }

  if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU)) {
    /* Doesn't support the IOMMU driver we want. */
    die("VFIO extension unsupported");
  }

  /* Open the group */
  group = open("/dev/vfio/29", O_RDWR);
  if (group < 0) {
    die("Cannot open /dev/vfio/29");
  }
  this->group = group;

  /* Test the group is viable and available */
  ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
  if (ret < 0) {
    die("Cannot get vfio satus");
  }

  if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
    /* Group is not viable (ie, not all devices bound for vfio) */
    die("Some flag missing");
  }

  /* Add the group to the container */
  ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
  if (ret < 0) {
    die("Cannot set vfio container");
  }

  /* Enable the IOMMU model we want */
  ret = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
  if (ret < 0) {
    die("Cannot set iommu type");
  }

  /* Get addition IOMMU info */
  ret = ioctl(container, VFIO_IOMMU_GET_INFO, &iommu_info);
  if (ret < 0) {
    die("Cannot get iommu info");
  }

  /* Allocate some space and setup a DMA mapping */
  dma_map.vaddr = (uint64_t)(mmap(0, 1024 * 1024, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, 0, 0));
  if (dma_map.vaddr == (uint64_t) MAP_FAILED) {
    die("Cannot allocate memory for DMA map");
  }
  dma_map.size = 1024 * 1024;
  dma_map.iova = 0; /* 1MB starting at 0x0 from device view */
  dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

  ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
  if (ret < 0) {
    die("Cannot set dma map");
  }
  this->dma_map = dma_map;

  /* Get a file descriptor for the device */
  device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, "0000:18:00.0");
  if (device < 0) {
    die("Cannot get/find device id in IOMMU group fd %d", group);
  }
  this->device = device;

  /* Test and setup the device */
  ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
  if (ret < 0) {
    die("Cannot get device info for device %d", device);
  }

  printf("\nDevice regions: %d\n\n", device_info.num_regions);

  for (i = 0; i < device_info.num_regions; i++) {
          struct vfio_region_info reg = { .argsz = sizeof(reg) };

          reg.index = i;

          ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);
          __builtin_dump_struct(&reg, &printf);
          this->regions.push_back(reg);

          /* Setup mappings... read/write offsets, mmaps
           * For PCI devices, config space is a region */
  }

  printf("\nDevice irsq: %d\n\n", device_info.num_irqs);

  for (i = 0; i < device_info.num_irqs; i++) {
          struct vfio_irq_info irq = { .argsz = sizeof(irq) };

          irq.index = i;

          ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq);
          __builtin_dump_struct(&irq, &printf);
          this->interrupts.push_back(irq);

          /* Setup IRQs... eventfds, VFIO_DEVICE_SET_IRQS */
  }

  /* Gratuitous device reset and go... */
  ioctl(device, VFIO_DEVICE_RESET);

  return 0;
}

int VfioConsumer::init_mmio() {
  // Only iterate bars 0-5. Bar >=6 seems not mappable. 
  for (int i = 0; i <= 5; i++) { 
    auto region = this->regions[i];
    if (region.size == 0) {
      printf("Mapping region BAR %d skipped\n", region.index);
      continue;
    }
    void* mem = mmap(NULL, region.size, PROT_READ | PROT_WRITE, MAP_SHARED, this->device, region.offset);
    if (mem == MAP_FAILED) {
      die("failed to map mmio region BAR %d via vfio", region.index);
    }
    this->mmio[region.index] = mem;
    printf("Vfio: Mapping region BAR %d offset 0x%llx size 0x%llx\n", region.index, region.offset, region.size);
  }
  return 0;
}

void VfioConsumer::init_msix() {
  int ret;

  uint32_t count = this->interrupts[VFIO_PCI_MSIX_IRQ_INDEX].count;
  if (count <= 0) {
    die("We expect devices to use MSIX IRQs/interrupts. This one doesnt right now");
  }

  auto irq_set_buf_size = sizeof(struct vfio_irq_set) + sizeof(int) * count;
  auto irq_set = (struct vfio_irq_set*) malloc(irq_set_buf_size);
  irq_set->argsz = irq_set_buf_size;
  irq_set->count = count;
  irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
  irq_set->index = VFIO_PCI_MSIX_IRQ_INDEX;
  irq_set->start = 0;

  for (uint64_t i = 0; i < count; i++) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) {
      if (errno == EMFILE) {
        die("Cant create eventfd nr. %lu. Use `ulimit -n` to set a higher limit", i);
      }
      die("Failed to create eventfd");
    }
    this->irqfds.push_back(fd);
  }

  memcpy((int*)&irq_set->data, this->irqfds.data(), sizeof(int) * count);
  ret = ioctl(this->device, VFIO_DEVICE_SET_IRQS, irq_set);
  if (ret < 0) {
    die("Cannot set eventfds for MSIX interrupts for device %d", this->device);
  }

  printf("Eventfds registered for %d MSIX interrupts.\n", count);

}

void VfioConsumer::mask_irqs(uint32_t irq_type, uint32_t start, uint32_t count, bool mask) {
  uint32_t flags = VFIO_IRQ_SET_DATA_NONE;
  if (mask) {
    flags |= VFIO_IRQ_SET_ACTION_MASK;
  } else {
    flags |= VFIO_IRQ_SET_ACTION_UNMASK;
  }

  struct vfio_irq_set irq_set;
  memset(&irq_set, 0, sizeof(irq_set)); // zero also .data
  irq_set.argsz = sizeof(irq_set);
  irq_set.flags = flags;
  irq_set.index = irq_type;
  irq_set.start = start;
  irq_set.count = count;
  //irq_set.data = { 0, 0 };
  __builtin_dump_struct(&irq_set, &printf);
  int ret = ioctl(this->device, VFIO_DEVICE_SET_IRQS, &irq_set);
  if (ret < 0)
    die("failed to mask irq");
}

void VfioConsumer::reset_device() {
  // TODO check if device supports reset VFIO_DEVICE_FLAGS_RESET
  int ret = ioctl(this->device, VFIO_DEVICE_RESET, NULL);
  if (ret < 0)
    die("failed to reset device");
}

void VfioConsumer::map_dma(vfio_iommu_type1_dma_map *dma_map) {
  int ret = ioctl(this->container, VFIO_IOMMU_MAP_DMA, dma_map);
  if (ret < 0)
    die("vfio failed to map dma");
}

void VfioConsumer::unmap_dma(vfio_iommu_type1_dma_unmap *dma_unmap) {
  int ret = ioctl(this->container, VFIO_IOMMU_UNMAP_DMA, dma_unmap);
  // TODO check dma_unmap size as well
  if (ret < 0)
    die("vfio failed to unmap dma");
}
