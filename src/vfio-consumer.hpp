#pragma once

#include <linux/vfio.h>
#include <map>
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

extern "C" {
#include "libvfio-user.h"
}

#define VFIOC_SECRET 1337
#define PCIE_HEADER_LENGTH 4096

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
        bool use_msix = true;
        bool is_pcie = true;

        std::string group_str;
        std::string device_name; // pci address

        // vfio fds
        int container;
        int group;
        int device;

        VfioConsumer(std::string group_str, std::string device_name) {
            this->group_str = "/dev/vfio/" + group_str;
            this->device_name = device_name;
        }
        VfioConsumer(std::string device_name) {
            this->group_str = "/dev/vfio/" + get_iommu_group(device_name);
            this->device_name = device_name;
        }
        ~VfioConsumer() {
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

        int init() {
            int ret, container, group, device;
            uint32_t i;
            struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
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
            group = open(group_str.c_str(), O_RDWR);
            if (group < 0) {
                die("Cannot open %s",group_str.c_str());
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

            //ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
            //if (ret < 0) {
            //die("Cannot set dma map");
            //}
            this->dma_map = dma_map;

            /* Get a file descriptor for the device */
            device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, device_name.c_str());
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

            // Enable DMA
            struct vfio_region_info* cs_info = &this->regions[VFIO_PCI_CONFIG_REGION_INDEX];
            char buf[2];
            pread(device, buf, 2, cs_info->offset + 4);
            *(uint16_t*)(buf) |= 1 << 2;
            pwrite(device, buf, 2, cs_info->offset + 4);

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

            //Get Header size to determine if the device is PCI or PCIe
            std::string config_path =
                "/sys/bus/pci/devices/" + this->device_name + "/config";
            FILE *fd = fopen(config_path.c_str(), "rb");
            if (fd == NULL)
                die("Cannot open %s", config_path.c_str());


            char* tmp[PCIE_HEADER_LENGTH];
            size_t size = fread(tmp, sizeof(char), PCIE_HEADER_LENGTH, fd);
            switch (size)
            {
                case 256:
                    printf("Device is PCI only\n");
                    this->is_pcie = false;
                    this->use_msix = false;
                    break;
                case PCIE_HEADER_LENGTH:
                    printf("Device is PCIe\n");
                    this->is_pcie = true;
                    this->use_msix = true;
                    break;  
                default:
                    die("only %zu bytes read", size);
                    break;
            }
            fclose(fd);

            return 0;
        }

        int init_mmio() {
            // Only iterate bars 0-5. Bar >=6 seems not mappable. 
            int num_regions = 5;
            if(!this->is_pcie){
                num_regions = 0;
            }
            for (int i = 0; i <= num_regions; i++) { 
                auto region = this->regions[i];
                if (region.size == 0) {
                    printf("Mapping region BAR %d skipped\n", region.index);
                    continue;
                }
                void* mem = mmap(NULL, region.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        this->device, region.offset);
                if (mem == MAP_FAILED) {
                    die("failed to map mmio region BAR %d via vfio", region.index);
                }
                this->mmio[region.index] = mem;
                printf("Vfio: Mapping region BAR %d offset 0x%llx size 0x%llx\n",
                        region.index, region.offset, region.size);
            }
            return 0;
        }

        void init_msix() {
            if (!this->use_msix) {
                printf("init for msix skipped because we run in intx mode\n");
                return; 
            }

            uint32_t count = this->interrupts[VFIO_PCI_MSIX_IRQ_INDEX].count;
            if (count <= 0) {
                die("We expect devices to use MSIX IRQs/interrupts. \
                        This one doesnt right now");
            }

            vfio_set_irqs(VFIO_PCI_MSIX_IRQ_INDEX, count, &this->irqfds, this->device);

            printf("Eventfds registered for %d MSIX interrupts.\n", count);
        }

        void init_legacy_irqs() {
            std::vector<int> irqfds;
            // registering INTX and MSI fails with EINVAL. Experimentation shows, that
            // only one of INTX, MSI or MSIX can be registered. MSI and MSIX must
            // indeed not be used simultaniousely by software (pci 4.0 section 6.1.4
            // MSI and MSI-X Operation). Simultaneous use of INTX and MSI(X) seems to
            // be a shortcoming in libvfio-user right now. See
            // https://github.com/nutanix/libvfio-user/issues/388 about insufficient
            // irq impl and https://github.com/nutanix/libvfio-user/issues/387 about no
            // way to trigger ERR or REQ irqs.
            if (!this->use_msix) {
                if (this->interrupts[VFIO_PCI_INTX_IRQ_INDEX].count != 1) {
                    die("This device does not support legacy INTx");
                }
                vfio_set_irqs(VFIO_PCI_INTX_IRQ_INDEX, 1, &irqfds, this->device);
                this->irqfd_intx = irqfds.back();
                irqfds.clear();
            }
            // vfio_set_irqs(VFIO_PCI_MSI_IRQ_INDEX, 1, &irqfds, this->device);
            // this->irqfd_msi = irqfds.back();
            // irqfds.clear();
            vfio_set_irqs(VFIO_PCI_ERR_IRQ_INDEX, 1, &irqfds, this->device);
            this->irqfd_err = irqfds.back();
            irqfds.clear();
            vfio_set_irqs(VFIO_PCI_REQ_IRQ_INDEX, 1, &irqfds, this->device);
            this->irqfd_req = irqfds.back();
        }

        void reset_device() {
            // TODO check if device supports reset VFIO_DEVICE_FLAGS_RESET
            int ret = ioctl(this->device, VFIO_DEVICE_RESET, NULL);

            if (ret < 0)
                printf("failed to reset device\n");
            //TODO
        }

        void map_dma(vfio_iommu_type1_dma_map *dma_map) {
            int ret = ioctl(this->container, VFIO_IOMMU_MAP_DMA, dma_map);
            if (ret < 0){
                printf("\033[31mvifo failed to map dma, %d: %s\033[0m\n",
                        errno, strerror(errno));
                printf("\033[31m");
                __builtin_dump_struct(dma_map, &printf);
                printf("\033[0m");
                return;
                die("vfio failed to map dma, %d",errno);

            }
        }

        void unmap_dma(vfio_iommu_type1_dma_unmap *dma_unmap) {
            int ret = ioctl(this->container, VFIO_IOMMU_UNMAP_DMA, dma_unmap);
            // TODO check dma_unmap size as well
            if (ret < 0)
                die("vfio failed to unmap dma");
        }

        void mask_irqs(uint32_t irq_type, uint32_t start, uint32_t count, bool mask) {
            if (irq_type == VFIO_PCI_INTX_IRQ_INDEX && this->use_msix) {
                printf("irq_state_cb for intx ignored because we run in msi-x mode\n");
                return; 
            }

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

        static void vfio_set_irqs(const int irq_type, const size_t count,
                std::vector<int> *irqfds, int device_fd)
        {
            int ret;

            auto irq_set_buf_size = sizeof(struct vfio_irq_set) + sizeof(int) * count;
            auto irq_set = (struct vfio_irq_set*) malloc(irq_set_buf_size);
            irq_set->argsz = irq_set_buf_size;
            irq_set->count = count;
            irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
            irq_set->index = irq_type;
            irq_set->start = 0;

            for (uint64_t i = 0; i < count; i++) {
                int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (fd < 0) {
                    if (errno == EMFILE) {
                        die("Cant create eventfd nr. \
                                %lu. Use `ulimit -n` to set a higher limit", i);
                    }
                    die("Failed to create eventfd");
                }
                irqfds->push_back(fd);
            }

            memcpy((int*)&irq_set->data, irqfds->data(), sizeof(int) * count);
            __builtin_dump_struct(irq_set, &printf);
            printf("irqfds %d-%d (#%zu)\n",
                    irqfds->front(), irqfds->back(),irqfds->size());
            ret = ioctl(device_fd, VFIO_DEVICE_SET_IRQS, irq_set);
            if (ret < 0) {
                //TODO: IRQs don't work properly on the e1000 yet
                printf("Cannot set eventfds for interrupts type %d for device %d\n",
                        irq_type, device_fd);
            }
        }
};

