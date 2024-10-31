#include "src/devices/vdpdk.hpp"
#include "libvfio-user.h"
#include "src/vfio-server.hpp"

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
  auto ctx = this->vfuServer->vfu_ctx;

  int region_flags = VFU_REGION_FLAG_READ | VFU_REGION_FLAG_MEM;
  int ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
                             0x1000, region_access_cb_static,
                             region_flags, NULL, 0,
                             -1, 0);
  if (ret)
    die("failed to setup BAR region (%d)", errno);
}

void VdpdkDevice::rx_callback_fn(int vm_number) {
  // TODO
}

void VdpdkDevice::rx_callback_static(int vm_number, void *this__) {
  VdpdkDevice *this_ = (VdpdkDevice *)this__;
  this_->rx_callback_fn(vm_number);
}

ssize_t VdpdkDevice::region_access_cb(char *buf, size_t count, loff_t offset, bool is_write) {
  if (is_write) return -1;

  strncpy(buf, "Hello from vmux", count);
  return count;
}

ssize_t VdpdkDevice::region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                       loff_t offset, bool is_write) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->region_access_cb(buf, count, offset, is_write);
}

