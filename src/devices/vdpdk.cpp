#include "src/devices/vdpdk.hpp"
#include "libvfio-user.h"
#include "src/vfio-server.hpp"

#include <rte_io.h>

enum VDPDK_OFFSET {
  DEBUG_STRING = 0x0,
};

enum VDPDK_CONSTS {
  REGION_SIZE = 0x1000,
  PKT_SIGNAL_OFF = REGION_SIZE - 0x40,
  MAX_PKT_LEN = PKT_SIGNAL_OFF,
};

VdpdkDevice::VdpdkDevice(int device_id, std::shared_ptr<Driver> driver)
: VmuxDevice(device_id, std::move(driver)),
  txbuf{"vdpdk_tx", REGION_SIZE},
  rxbuf{"vdpdk_rx", REGION_SIZE} {
  // TODO figure out appropriate IDs
  this->info.pci_vendor_id = 0x1af4; // Red Hat Virtio Devices
  this->info.pci_device_id = 0x7abc; // Unused
  this->info.pci_subsystem_vendor_id = 0;
  this->info.pci_subsystem_id = 0;
  this->info.pci_device_revision_id = 0;
  this->info.pci_class = 2;
  this->info.pci_subclass = 0;
  this->info.pci_revision = 1;

  rte_write8(1, rxbuf.ptr() + PKT_SIGNAL_OFF);

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
  if (ret) {
    die("failed to setup BAR0 region (%d)", errno);
  }

  ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR1_REGION_IDX,
                             txbuf.size(), NULL,
                             region_flags, NULL, 0,
                             txbuf.fd(), 0);
  if (ret) {
    die("failed to setup BAR1 region (%d)", errno);
  }

  ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR2_REGION_IDX,
                             rxbuf.size(), NULL,
                             region_flags, NULL, 0,
                             rxbuf.fd(), 0);
  if (ret) {
    die("failed to setup BAR2 region (%d)", errno);
  }

  ret = vfu_setup_device_dma(ctx, dma_register_cb_static,
                             dma_unregister_cb_static);
  if (ret) {
    die("failed to setup device dma (%d)", errno);
  }

  tx_poll_thread = std::jthread{[this](std::stop_token stop){
    this->tx_poll(std::move(stop));
  }};
}

void VdpdkDevice::rx_callback_fn(int vm_number) {
  uint8_t *lock = (uint8_t *)(rxbuf.ptr() + PKT_SIGNAL_OFF);

  // Only try locking for a bit
  // If we can't lock, we wan't to return to give control back to the RX thread
  bool locked = false;
  for (unsigned i = 0; i < 2048; i++) {
    if (rte_read8(lock) == 0) {
      locked = true;
      break;
    }
  }
  if (!locked) {
    return;
  }

  driver->recv(vm_number);

  unsigned char *ptr = rxbuf.ptr();
  unsigned char *end = ptr + MAX_PKT_LEN;
  constexpr size_t addr_size = sizeof(uintptr_t);

  bool have_buffers = true;
  for (int q_idx = 0; q_idx < 4 && have_buffers; q_idx++) { // TODO hardcoded max_queues_per_vm
    int queue_id = vm_number * 4 + q_idx;// TODO this_->get_rx_queue_id(vm_id, q_idx);
    for (uint16_t i = queue_id * 32; i < (queue_id * 32+ driver->nb_bufs_used[queue_id]); i++) { // TODO 32 = BURST_SIZE
      // this_->model->EthRx(0, this_->driver->rxBuf_queue[i], this_->driver->rxBufs[i], this_->driver->rxBuf_used[i]); // hardcode port 0

      if (ptr + 2 + addr_size > end) {
        have_buffers = false;
        break;
      }

      uint16_t max_pkt_len = ptr[0] | ((uint16_t)ptr[1] << 8);
      if (max_pkt_len == 0) {
        have_buffers = false;
        break;
      }

      uintptr_t dma_addr;
      memcpy(&dma_addr, ptr + 2, addr_size);
      void *data_ptr = vfuServer->dma_local_addr(dma_addr, max_pkt_len);
      if (data_ptr == NULL) {
        printf("Invalid DMA address (%lx)\n", (unsigned long)dma_addr);
        have_buffers = false;
        break;
      }
      size_t pkt_len = driver->rxBuf_used[i];
      if (pkt_len > max_pkt_len) {
        printf("Packet too large (%lx > %x)\n", (unsigned long)pkt_len, (unsigned)max_pkt_len);
        have_buffers = false;
        break;
      }

      memcpy(data_ptr, driver->rxBufs[i], pkt_len);
      ptr[0] = pkt_len;
      ptr[1] = pkt_len >> 8;
      ptr += 2 + addr_size;
    }
  }

  if (ptr + 2 + addr_size <= end) {
    ptr[0] = 0;
    ptr[1] = 0;
  }

  rte_write8(1, lock);
  driver->recv_consumed(vm_number);
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
  }

  printf("Invalid write offset: %x\n", offset);
  return -1;
}

ssize_t VdpdkDevice::region_access_read(char *buf, size_t count, unsigned offset) {
  if (count == 0) return -1;
  if (offset < 0x40) {
    char msg[] = "Hello from vmux";
    static_assert(sizeof(msg) <= 0x40);

    if (offset >= sizeof(msg)) {
      memset(buf, 0, count);
    } else {
      strncpy(buf, msg + offset, count);
    }
    return count;
  }

  // switch (offset) {
  //   case TX_SIGNAL: {
  //     uint16_t pkt_len = txbuf[PKT_LEN] | ((uint16_t)txbuf[PKT_LEN + 1] << 8);
  //     driver->send(device_id, (const char *)txbuf.ptr(), pkt_len);
  //     memset(buf, 0, count);
  //     buf[0] = 1;
  //     return count;
  //   }
  // }

  printf("Invalid read offset: %x\n", offset);
  return -1;
}

void VdpdkDevice::tx_poll(std::stop_token stop) {
  uint8_t *lock = (uint8_t *)(txbuf.ptr() + PKT_SIGNAL_OFF);
  while (true) {
    while (rte_read8(lock) != 0) {
      if (stop.stop_requested()) return;
    }

    unsigned char *ptr = txbuf.ptr();
    unsigned char *end = ptr + MAX_PKT_LEN;
    constexpr size_t addr_size = sizeof(uintptr_t);

    while (ptr + addr_size + 2 <= end) {
      uint16_t pkt_len = ptr[0] | ((uint16_t)ptr[1] << 8);
      ptr += 2;
      if (pkt_len == 0) {
        break;
      }

      uintptr_t dma_addr;
      memcpy(&dma_addr, ptr, addr_size);
      const char *data_ptr = (const char *)vfuServer->dma_local_addr(dma_addr, pkt_len);
      if (data_ptr == NULL) {
        printf("Invalid DMA address (%lx)\n", (unsigned long)dma_addr);
        break;
      }
      ptr += addr_size;

      driver->send(device_id, data_ptr, pkt_len);
    }

    rte_write8(1, lock);
  }
}

void VdpdkDevice::dma_register_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  uint32_t flags = 0;
  VfioUserServer::map_dma_here(ctx, vfuServer.get(), info, &flags);
}

void VdpdkDevice::dma_register_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->dma_register_cb(ctx, info);
}

void VdpdkDevice::dma_unregister_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  uint32_t flags = 0;
  VfioUserServer::unmap_dma_here(ctx, vfuServer.get(), info);
}

void VdpdkDevice::dma_unregister_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->dma_unregister_cb(ctx, info);
}
