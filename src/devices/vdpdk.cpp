#include "src/devices/vdpdk.hpp"

void VdpdkDevice::rx_callback_static(int vm_number, void *this__) {
  VdpdkDevice *this_ = (VdpdkDevice *)this__;
  this_->rx_callback_fn(vm_number);
}

VdpdkDevice::VdpdkDevice(int device_id, std::shared_ptr<Driver> driver)
: VmuxDevice(device_id, std::move(driver)) {
  // TODO figure out appropriate IDs
  this->info.pci_vendor_id = 0x1af4; // Red Hat Virtio Devices
  this->info.pci_device_id = 0x7abc; // Unused
  this->info.pci_subsystem_vendor_id = 0;
  this->info.pci_subsystem_id = 0;
  this->info.pci_device_revision_id = 0;
  this->info.pci_class = 2;
  this->info.pci_subclass = 0;
  this->info.pci_revision = 1;

  this->rx_callback = rx_callback_static;
}

void VdpdkDevice::setup_vfu(std::shared_ptr<VfioUserServer> vfu) {
  this->vfuServer = std::move(vfu);
}

void VdpdkDevice::rx_callback_fn(int vm_number) {
  // TODO
}
