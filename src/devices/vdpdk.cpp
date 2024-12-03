#include "src/devices/vdpdk.hpp"
#include "libvfio-user.h"
#include "src/vfio-server.hpp"

#include <rte_io.h>
#include <rte_mempool.h>
#include <format>

enum VDPDK_OFFSET {
  DEBUG_STRING = 0x0,
  TX_QUEUE_START = 0x40,
  TX_QUEUE_STOP = 0x80,

  RX_QUEUE_START = 0x140,
  RX_QUEUE_STOP = 0x180,
};

enum VDPDK_CONSTS {
  REGION_SIZE = 0x1000,

  TX_DESC_SIZE = 0x20,
  TX_FLAG_AVAIL = 1,

  RX_DESC_SIZE = 0x20,
  RX_FLAG_AVAIL = 1,
};

VdpdkDevice::VdpdkDevice(int device_id, std::shared_ptr<Driver> driver)
: VmuxDevice(device_id, driver),
  txbuf{"vdpdk_tx", REGION_SIZE},
  rxbuf{"vdpdk_rx", REGION_SIZE},
  dpdk_driver(std::dynamic_pointer_cast<Dpdk>(driver)) {
  if (!dpdk_driver) {
    die("Using vDPDK without the DPDK backend is not supported.");
  }

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
}

void VdpdkDevice::rx_callback_fn(int vm_number) {
  driver->recv(vm_number);

  // We delay loading this until we actually know if packets were received
  std::shared_lock dma_lock(dma_mutex, std::defer_lock);
  std::shared_ptr<RxQueue> rxq;
  size_t ring_size;
  unsigned char *ring;

  bool have_buffers = true;
  for (unsigned q_idx = 0; q_idx < driver->max_queues_per_vm && have_buffers; q_idx++) {
    auto &driver_rxq = driver->get_rx_queue(vm_number, q_idx);
    for (uint16_t i = 0; i < driver_rxq.nb_bufs_used; i++) {
      // If we reach this point, at least one packet was received
      auto &driver_rxBuf = driver_rxq.rxBufs[i];

      // Lock and load rx_queue parameters
      if (!rxq) {
        rxq = rx_queue.load();
        // If no queue was created, we are done
        if (!rxq) {
          have_buffers = false;
          break;
        }

        dma_lock.lock();
        ring_size = ((size_t)rxq->idx_mask + 1) * RX_DESC_SIZE;
        ring = (unsigned char *)vfuServer->dma_local_addr(rxq->ring_iova, ring_size);
        if (!ring) {
          printf("DMA unmapped during RX poll\n");
          have_buffers = false;
          break;
        }
      }

      unsigned char *buf_iova_addr = ring + (size_t)(rxq->idx & rxq->idx_mask) * RX_DESC_SIZE;
      unsigned char *buf_len_addr = buf_iova_addr + 8;
      unsigned char *desc_flags_addr = buf_len_addr + 2;

      uint16_t flags = rte_read16(desc_flags_addr);
      // If next descriptor is not available, we are out of buffers
      if (!(flags & RX_FLAG_AVAIL)) {
        have_buffers = false;
        break;
      }

      // If FLAG_AVAIL is set, we own the buffer and can copy the packet into it
      uint64_t buf_iova;
      memcpy(&buf_iova, buf_iova_addr, 8);
      uint16_t buf_len;
      memcpy(&buf_len, buf_len_addr, 2);
      void *buf_addr = vfuServer->dma_local_addr(buf_iova, buf_len);
      if (!buf_addr) {
        printf("Invalid packet iova!\n");
        have_buffers = false;
        break;
      }

      // Check sizes
      size_t pkt_len = driver_rxBuf.used;
      uint16_t pkt_len_u16 = pkt_len;
      if (pkt_len > buf_len || pkt_len > pkt_len_u16) {
        printf("Packet too large (%lx > %x)\n", (unsigned long)pkt_len, (unsigned)buf_len);
        have_buffers = false;
        break;
      }

      // Copy data
      memcpy(buf_addr, driver_rxBuf.data, pkt_len);
      memcpy(buf_len_addr, &pkt_len_u16, 2);

      // Release buffer back to VM
      flags &= ~RX_FLAG_AVAIL;
      rte_write16(flags, desc_flags_addr);

      // Go to next descriptor
      // Index wraps naturally on overflow
      rxq->idx++;
    }
  }

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

    case TX_QUEUE_START: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx != 0) {
        printf("TX_QUEUE_START: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      uint64_t ring_addr;
      memcpy(&ring_addr, txbuf.ptr(), 8);
      uint16_t idx_mask;
      memcpy(&idx_mask, txbuf.ptr() + 8, 2);

      // Start polling
      tx_poll_thread = std::jthread{[this, ring_addr, idx_mask](std::stop_token stop){
        this->tx_poll(std::move(stop), ring_addr, idx_mask);
      }};
      return count;
    }

    case TX_QUEUE_STOP: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx != 0) {
        printf("TX_QUEUE_STOP: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      // Stop polling
      tx_poll_thread = std::jthread{};
      return count;
    }

    case RX_QUEUE_START: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx != 0) {
        printf("RX_QUEUE_START: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      uint64_t ring_addr;
      memcpy(&ring_addr, rxbuf.ptr(), 8);
      uint16_t idx_mask;
      memcpy(&idx_mask, rxbuf.ptr() + 8, 2);

      auto rxq = std::make_shared<RxQueue>();
      rxq->ring_iova = ring_addr;
      rxq->idx_mask = idx_mask;
      rxq->idx = 0;

      rx_queue = rxq;

      return count;
    }

    case RX_QUEUE_STOP: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx != 0) {
        printf("RX_QUEUE_STOP: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      rx_queue = nullptr;
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

void VdpdkDevice::tx_poll(std::stop_token stop, uintptr_t ring_iova, uint16_t idx_mask) {
  size_t ring_size = ((size_t)idx_mask + 1) * TX_DESC_SIZE;

  puts(std::format("Start TX polling with iova {:#x}, mask {:#x}, size {:#x}", ring_iova, idx_mask, ring_size).c_str());

  std::shared_lock dma_lock(dma_mutex);
  unsigned char *ring = (unsigned char *)vfuServer->dma_local_addr(ring_iova, ring_size);
  if (!ring) {
    printf("Invalid ring_iova\n");
    return;
  }
  uint16_t idx = 0;
  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      return;
    }

    // Check if DMA mapping wants to change
    if (dma_flag.test()) {
      // Release lock
      dma_lock.unlock();
      // Wait until vfio-user thread holds mutex
      while (dma_flag.test());
      // Re-aquire lock
      dma_lock.lock();
      // Ensure ring address is still valid
      ring = (unsigned char *)vfuServer->dma_local_addr(ring_iova, ring_size);
      if (!ring) {
        printf("DMA unmapped during TX poll\n");
        return;
      }
    }

    unsigned char *buf_iova_addr = ring + (size_t)(idx & idx_mask) * TX_DESC_SIZE;
    unsigned char *buf_len_addr = buf_iova_addr + 8;
    unsigned char *desc_flags_addr = buf_len_addr + 2;

    uint16_t flags = rte_read16(desc_flags_addr);
    // If next descriptor is not available, try again
    if (!(flags & TX_FLAG_AVAIL)) {
      continue;
    }

    // If FLAG_AVAIL is set, we own the buffer and need to send it
    uint64_t buf_iova;
    memcpy(&buf_iova, buf_iova_addr, 8);
    uint16_t buf_len;
    memcpy(&buf_len, buf_len_addr, 2);
    void *buf_addr = vfuServer->dma_local_addr(buf_iova, buf_len);
    if (!buf_addr) {
      printf("Invalid packet iova!\n");
      return;
    }

    driver->send(device_id, (const char *)buf_addr, buf_len);

    // Release buffer back to VM
    flags &= ~TX_FLAG_AVAIL;
    rte_write16(flags, desc_flags_addr);

    // Go to next descriptor
    // Index wraps naturally on overflow
    idx++;
  }
}

void VdpdkDevice::dma_register_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  dma_flag.test_and_set();
  std::lock_guard guard(dma_mutex);
  dma_flag.clear();
  uint32_t flags = 0;
  VfioUserServer::map_dma_here(ctx, vfuServer.get(), info, &flags);
}

void VdpdkDevice::dma_register_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->dma_register_cb(ctx, info);
}

void VdpdkDevice::dma_unregister_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  dma_flag.test_and_set();
  std::lock_guard guard(dma_mutex);
  dma_flag.clear();
  uint32_t flags = 0;
  VfioUserServer::unmap_dma_here(ctx, vfuServer.get(), info);
}

void VdpdkDevice::dma_unregister_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info) {
  VdpdkDevice *this_ = (VdpdkDevice *)vfu_get_private(ctx);
  return this_->dma_unregister_cb(ctx, info);
}

// TX MEMPOOL ADAPTER

struct vdpdk_tx_mempool_private {
  // Drivers may expect this as the memory pool's private data.
  // By making it the first member, any pointer to vdpdk_tx_mempool_private
  // is also a valid pointer to rte_pktmbuf_pool_private.
  struct rte_pktmbuf_pool_private pkmbuf_private;

  struct rte_mempool_ops *inner;
};

int vdpdk_tx_mempool_alloc(struct rte_mempool *mp) {
  auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
  return priv->inner->alloc(mp);
}

void vdpdk_tx_mempool_free(struct rte_mempool *mp) {
  auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
  priv->inner->free(mp);
}

int vdpdk_tx_mempool_enqueue(struct rte_mempool *mp, void *const *obj_table, unsigned int n) {
  auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
  return priv->inner->enqueue(mp, obj_table, n);
}

int vdpdk_tx_mempool_dequeue(struct rte_mempool *mp, void **obj_table, unsigned int n) {
  auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
  return priv->inner->dequeue(mp, obj_table, n);
}

unsigned vdpdk_tx_mempool_get_count(const struct rte_mempool *mp) {
  auto priv = (const struct vdpdk_tx_mempool_private *)
    rte_mempool_get_priv((struct rte_mempool *)mp);
  return priv->inner->get_count(mp);
}

// This memory pool is a wrapper around a regular dpdk memory pool.
// It exists to detect when a driver frees a pktmbuf. The freed pktmbuf is then
// handed back to the guest VM.
struct rte_mempool_ops vdpdk_tx_mempool {
 .name = "VDPDK_TX_MEMPOOL",
 .alloc = vdpdk_tx_mempool_alloc,
 .free = vdpdk_tx_mempool_free,
 .enqueue = vdpdk_tx_mempool_enqueue,
 .dequeue = vdpdk_tx_mempool_dequeue,
 .get_count = vdpdk_tx_mempool_get_count,
};

RTE_MEMPOOL_REGISTER_OPS(vdpdk_tx_mempool);
