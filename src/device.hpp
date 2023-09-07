#pragma once

#include "util.hpp"
#include "vfio-consumer.hpp"
#include <cstdint>
#include <memory>
#include <string>

/** Number of PCI bars */
#define SIMBRICKS_PROTO_PCIE_NBARS 6

/* TODO Some of these fields may be unused */
struct DeviceInfo {
  /** information for each BAR exposed by the device */
  struct {
    /** length of the bar in bytes (len = 0 indicates unused bar) */
    uint64_t len;
    /** flags (see SIMBRICKS_PROTO_PCIE_BAR_*) */
    uint64_t flags;
  } __attribute__((packed)) bars[SIMBRICKS_PROTO_PCIE_NBARS];

  /** PCI vendor id */
  uint16_t pci_vendor_id;
  /** PCI device id */
  uint16_t pci_device_id;
  /* PCI device class */
  uint8_t pci_class;
  /* PCI device subclass */
  uint8_t pci_subclass;
  /* PCI device revision */
  uint8_t pci_revision;
  /* PCI prog if */
  uint8_t pci_progif;

  /* PCI number of MSI vectors */
  uint8_t pci_msi_nvecs;

  /* PCI number of MSI-X vectors */
  uint16_t pci_msix_nvecs;
  /* BAR number for MSI-X table */
  uint8_t pci_msix_table_bar;
  /* BAR number for MSI-X PBA */
  uint8_t pci_msix_pba_bar;
  /* Offset for MSI-X table */
  uint32_t pci_msix_table_offset;
  /* Offset for MSI-X PBA */
  uint32_t pci_msix_pba_offset;
  /* MSI-X capability offset */
  uint16_t psi_msix_cap_offset;
};

class VmuxDevice {
  public:
    DeviceInfo info;

    /* vfio endpoint, may be null for some devices */
    std::shared_ptr<VfioConsumer> vfioc;

};

class PassthroughDevice : public VmuxDevice {
  public:
    PassthroughDevice(std::shared_ptr<VfioConsumer> vfioc, std::string pci_address) {
      this->vfioc = vfioc;
      this->init_pci_ids(pci_address);
    }

  private:
    void init_pci_ids(std::string device) {
      std::string group_arg = get_iommu_group(device);

      //Get Hardware Information from Device
      std::vector<int> pci_ids = get_hardware_ids(device,group_arg);
      if(pci_ids.size() != 5){
          die("Failed to parse Hardware Information, expected %d IDs got %zu\n",
                  5, pci_ids.size());
          // stop_runner(-1,
          // "Failed to parse Hardware Information, expected %d IDs got %zu\n",
          // 5, pci_ids.size());
      }
      this->info.pci_revision = pci_ids[0];
      pci_ids.erase(pci_ids.begin()); // Only contains Vendor ID, Device ID,
                                      // Subsystem Vendor ID, Subsystem ID now
      this->info.pci_vendor_id = pci_ids[0];
      this->info.pci_device_id = pci_ids[1];
      this->info.pci_class = pci_ids[2];
      this->info.pci_subclass = pci_ids[3];

      printf("PCI-Device: %s\nIOMMU-Group: %s\nRevision: 0x%02X\n\
              IDs: 0x%04X,0x%04X,0x%04X,0x%04X\n",
              device.c_str(),
              group_arg.c_str(),
              this->info.pci_revision,
              this->info.pci_vendor_id,
              this->info.pci_device_id,
              this->info.pci_class,
              this->info.pci_subclass);

    }
};
