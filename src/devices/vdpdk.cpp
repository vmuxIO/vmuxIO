#include "src/devices/vdpdk.hpp"
#include "libvfio-user.h"
#include "src/vfio-server.hpp"

enum VDPDK_OFFSET {
  DEBUG_STRING = 0x0,
  PACKET_BEGIN = 0x40,
  PACKET_DATA = 0x80,
  PACKET_END = 0xc0,
  RX_LEN = 0x100,
};

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

  int region_flags = VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM;
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
  // printf("Region access: count %zx, offset %lx, is_write %d\n", count, (long)offset, (int)is_write);

  if (offset < 0 || offset > 0x1000) return -1;
  if ((size_t)(0x1000 - offset) < count) return -1;

  if (is_write) {
    return region_access_write(buf, count, offset);
  }
  
  return region_access_read(buf, count, offset);
}

ssize_t VdpdkDevice::region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                       loff_t offset, bool is_write) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->region_access_cb(buf, count, offset, is_write);
}

ssize_t VdpdkDevice::region_access_write(char *buf, size_t count, unsigned offset) {
  switch (offset) {
  case DEBUG_STRING: {
    bool is_terminal = memchr(buf, 0, count) != NULL;
    dbg_string.append(buf, count);
    if (is_terminal) {
      printf("Received debug string: %s\n", dbg_string.c_str());
    }
    return count;
  }

  case PACKET_BEGIN:
    if (!pkt_buf.empty()) {
      puts("Packet buffer not empty on PACKET_BEGIN");
      pkt_buf.clear();
    }
    return count;

  case PACKET_DATA:
    pkt_buf.insert(pkt_buf.end(), buf, buf + count);
    return count;

  case PACKET_END:
    driver->send(device_id, (const char *)pkt_buf.data(), pkt_buf.size());
    pkt_buf.clear();
    return count;
  }

  printf("Invalid write offset: %x\n", offset);
  return -1;
}

ssize_t VdpdkDevice::region_access_read(char *buf, size_t count, unsigned offset) {
  if (offset >= PACKET_BEGIN) return -1;
  char msg[] = "Hello from vmux";
  static_assert(sizeof(msg) <= PACKET_BEGIN);

  if (offset >= sizeof(msg)) {
    memset(buf, 0, count);
  } else {
    strncpy(buf, msg + offset, count);
  }
  return count;
}
