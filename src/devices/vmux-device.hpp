#pragma once

#include "vfio-consumer.hpp"
// #include "vfio-server.hpp"
#include <cstdint>
#include <memory>

class VfioUserServer;

// /** Number of PCI bars */
// #define SIMBRICKS_PROTO_PCIE_NBARS 6

struct DeviceInfo {
  // /** information for each BAR exposed by the device */
  // struct {
  //   /** length of the bar in bytes (len = 0 indicates unused bar) */
  //   uint64_t len;
  //   /** flags (see SIMBRICKS_PROTO_PCIE_BAR_*) */
  //   uint64_t flags;
  // } __attribute__((packed)) bars[SIMBRICKS_PROTO_PCIE_NBARS];

  // Vendor ID, Device ID, Subsystem Vendor ID, and Subsystem ID
  /** PCI vendor id */
  uint16_t pci_vendor_id;
  /** PCI device id */
  uint16_t pci_device_id;
  /** PCI subsystem vendor id **/
  uint16_t pci_subsystem_vendor_id;
  /** PCI subsystem id **/
  uint16_t pci_subsystem_id;
  /** PCI device revision id **/
  uint8_t pci_device_revision_id;
  /* PCI class */
  uint8_t pci_class;
  /* PCI subclass */
  uint8_t pci_subclass;
  /* PCI revision */
  uint8_t pci_revision;
  // /* PCI prog if */
  // uint8_t pci_progif;
  //
  // /* PCI number of MSI vectors */
  // uint8_t pci_msi_nvecs;
  //
  // /* PCI number of MSI-X vectors */
  // uint16_t pci_msix_nvecs;
  // /* BAR number for MSI-X table */
  // uint8_t pci_msix_table_bar;
  // /* BAR number for MSI-X PBA */
  // uint8_t pci_msix_pba_bar;
  // /* Offset for MSI-X table */
  // uint32_t pci_msix_table_offset;
  // /* Offset for MSI-X PBA */
  // uint32_t pci_msix_pba_offset;
  // /* MSI-X capability offset */
  // uint16_t psi_msix_cap_offset;
};

class VmuxDevice {
public:
  DeviceInfo info;

  /* vfio endpoint, may be null for some devices */
  std::shared_ptr<VfioConsumer> vfioc;

  std::shared_ptr<VfioUserServer> vfuServer;

  virtual void setup_vfu(std::shared_ptr<VfioUserServer> vfu) = 0;

  virtual ~VmuxDevice() = default;
};

class StubDevice : public VmuxDevice {
public:
  StubDevice() { this->vfioc = NULL; }
  void setup_vfu(std::shared_ptr<VfioUserServer> vfu){};
};
