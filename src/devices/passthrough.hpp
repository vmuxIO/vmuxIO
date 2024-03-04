#pragma once

#include "libsimbricks/simbricks/nicbm/nicbm.h"
#include "sims/nic/e810_bm/e810_bm.h"
#include "src/devices/vmux-device.hpp"
#include "util.hpp"
#include "vfio-consumer.hpp"
#include "vfio-server.hpp"
#include <cstdint>
#include <memory>
#include <string>

class PassthroughDevice : public VmuxDevice {
public:
  PassthroughDevice(std::shared_ptr<VfioConsumer> vfioc,
                    std::string pci_address) : VmuxDevice(NULL) {
    this->vfioc = vfioc;
    this->init_pci_ids(pci_address);
  }

  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override {
    this->vfuServer = vfu;
    int ret;

    // set up vfio-user register passthrough
    if (this->vfioc != NULL) {
      // pass through registers, only if it is a passthrough device
      ret = vfu->add_regions(this->vfioc->regions, this->vfioc->device);
      if (ret < 0)
        die("failed to add regions");
    }

    // set up irqs
    if (this->vfioc != NULL) {
      ret = vfu->add_irqs(this->vfioc->interrupts);
      if (ret < 0)
        die("failed to add irqs");

      vfu->add_legacy_irq_pollfds(
          this->vfioc->irqfd_intx, this->vfioc->irqfd_msi,
          this->vfioc->irqfd_err, this->vfioc->irqfd_req);
      vfu->add_msix_pollfds(this->vfioc->irqfds);
    }

    // set up callbacks
    vfu->setup_passthrough_callbacks(this->vfioc);
  }

private:
  void init_pci_ids(std::string device) {
    std::string group_arg = Util::get_iommu_group(device);

    // Get Hardware Information from Device
    std::vector<int> pci_ids = Util::get_hardware_ids(device, group_arg);
    if (pci_ids.size() != 5) {
      die("Failed to parse Hardware Information, expected %d IDs got %zu\n", 5,
          pci_ids.size());
      // stop_runner(-1,
      // "Failed to parse Hardware Information, expected %d IDs got %zu\n",
      // 5, pci_ids.size());
    }
    this->info.pci_device_revision_id = pci_ids[0];
    pci_ids.erase(pci_ids.begin()); // Only contains Vendor ID, Device ID,
                                    // Subsystem Vendor ID, Subsystem ID now
    this->info.pci_vendor_id = pci_ids[0];
    this->info.pci_device_id = pci_ids[1];
    this->info.pci_subsystem_vendor_id = pci_ids[2];
    this->info.pci_subsystem_id = pci_ids[3];

    // sane defaults for pci (non-device) ids
    this->info.pci_class = 0x2;
    this->info.pci_subclass = 0x0;
    this->info.pci_revision = 0x0;
    __builtin_dump_struct(&this->info, &printf);

    printf("PCI-Device: %s\nIOMMU-Group: %s\nRevision: 0x%02X\n\
              IDs: 0x%04X,0x%04X,0x%04X,0x%04X\n",
           device.c_str(), group_arg.c_str(), this->info.pci_device_revision_id,
           this->info.pci_vendor_id, this->info.pci_device_id,
           this->info.pci_subsystem_vendor_id, this->info.pci_subsystem_id);
  }
};
