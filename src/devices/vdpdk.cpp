#include "src/devices/vdpdk.hpp"
#include "src/devices/vdpdk-consts.hpp"
#include "libvfio-user.h"
#include "src/vfio-server.hpp"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_io.h>
#include <rte_mbuf.h>
#include <rte_mbuf_pool_ops.h>
#include <rte_mempool.h>
#include <format>

using namespace VDPDK_CONSTS;

enum VDPDK_OFFSET {
  DEBUG_STRING = 0x0,
  TX_QUEUE_START = 0x40,
  TX_QUEUE_STOP = 0x80,

  RX_QUEUE_START = 0x140,
  RX_QUEUE_STOP = 0x180,
};

VdpdkDevice::VdpdkDevice(int device_id, std::shared_ptr<Driver> driver)
: VmuxDevice(device_id, driver),
  txbuf{"vdpdk_tx", REGION_SIZE},
  rxbuf{"vdpdk_rx", REGION_SIZE},
  dpdk_driver(std::dynamic_pointer_cast<Dpdk>(driver)) {
  if (!dpdk_driver) {
    die("Using vDPDK without the DPDK backend is not supported.");
  }
  if (rte_eal_iova_mode() != RTE_IOVA_VA) {
    die("vDPDK only supports virtual address IOVA mode. (Try using DPDK with --iova-mode=va)");
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

      // Stop left-over polling thread
      tx_poll_thread = {};
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
  constexpr bool DEBUG_OUTPUT = false;
  constexpr bool ZERO_COPY = false;

  size_t ring_size = ((size_t)idx_mask + 1) * TX_DESC_SIZE;

  puts(std::format("Start TX polling with iova {:#x}, mask {:#x}, size {:#x}", ring_iova, idx_mask, ring_size).c_str());

  std::shared_lock dma_lock(dma_mutex);
  unsigned char *ring = (unsigned char *)vfuServer->dma_local_addr(ring_iova, ring_size);
  if (!ring) {
    printf("Invalid ring_iova\n");
    return;
  }

  uint16_t queue_idx = dpdk_driver->get_tx_queue_id(device_id, 0);
  struct rte_mempool *pool = dpdk_driver->tx_mbuf_pools[queue_idx];
  assert(rte_pktmbuf_priv_size(pool) >= sizeof(struct rte_mbuf_ext_shared_info) + TX_DESC_SIZE);

  constexpr unsigned burst_size = 128;
  struct rte_mbuf *mbufs[burst_size];
  unsigned nb_mbufs_used = 0;

  struct queue_data {
    unsigned char *ring;
    uint16_t front_idx, back_idx;
    uint16_t idx_mask;
  } queue_data = {};
  queue_data.ring = ring;
  queue_data.idx_mask = idx_mask;

  uint16_t &idx = queue_data.back_idx;

  rte_mbuf_extbuf_free_callback_t free_cb = [](void *addr, void *opaque) {
    // opaque points to the copied descriptor, with the dma address replaced
    // with a pointer to the queue data;
    uintptr_t uptr;
    struct queue_data *queue_data;
    memcpy(&uptr, opaque, sizeof(uptr));
    queue_data = (struct queue_data *)uptr;

    unsigned char *desc_addr = queue_data->ring +
                               (size_t)(queue_data->front_idx & queue_data->idx_mask) * TX_DESC_SIZE;
    // TODO: can be removed
    // It should be impossible that a buffer is freed without a corresponding available descriptor
    uint16_t flags = rte_read16_relaxed(desc_addr + 10);
    assert(flags & TX_FLAG_AVAIL);

    // Copy descriptor into ring, skipping the dma/queue pointer
    rte_memcpy(desc_addr + 8, (char *)opaque + 8, TX_DESC_SIZE - 8);
    // Unset FLAG_AVAIL to return buffer to guest
    memcpy(&flags, (char *)opaque + 10, 2);
    flags &= ~TX_FLAG_AVAIL;
    rte_write16(flags, (char *)desc_addr + 10);

    queue_data->front_idx++;
  };

  constexpr unsigned debug_interval = 10000000;
  unsigned debug_counter = debug_interval;
  unsigned nb_cleanup_calls = 0;
  int last_cleanup_result = 0xFFFFFF;

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
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
        break;
      }
    }

    // Debug output
    if constexpr (DEBUG_OUTPUT) {
      if (--debug_counter == 0) {
        debug_counter = debug_interval;
        uint16_t last_flags = -1;
        uint16_t flags;
        unsigned min_idx = 0;
        printf("\nTX RING REPORT\n");
        for (uint16_t idx = 0; idx <= idx_mask + 1; idx++) {
          if (idx <= idx_mask) {
            auto flags_addr = ring + (size_t)idx * TX_DESC_SIZE + 10;
            memcpy(&flags, flags_addr, 2);
          }
          if ((flags != last_flags && idx != 0) || idx == idx_mask + 1) {
            const char *flag_str;
            if ((last_flags & TX_FLAG_AVAIL) && (last_flags & TX_FLAG_ATTACHED)) {
              flag_str = "AVAIL | ATTACHED";
            } else if (last_flags & TX_FLAG_AVAIL) {
              flag_str = "AVAIL";
            } else if (last_flags != 0) {
              flag_str = "ERROR";
            } else {
              flag_str = "";
            }

            printf("[%03x-%03x] %s (%x)\n", min_idx, idx - 1, flag_str, last_flags);
            min_idx = idx;
          }
          last_flags = flags;
        }
        printf("front: %x, back %x\n", queue_data.front_idx, queue_data.back_idx);

        printf("\nTX MBUF REPORT\n");
        unsigned mbuf_avail = rte_mempool_avail_count(pool);
        unsigned mbuf_in_use = pool->size - mbuf_avail;
        printf("available: %x\nin use: %x\n", mbuf_avail, mbuf_in_use);

        if (nb_cleanup_calls > 0) {
          printf("\nRING CLEANUP\n");
          printf("called: %u times\nlast result: %d\n", nb_cleanup_calls, last_cleanup_result);
          nb_cleanup_calls = 0;
        }
      }
    }

    unsigned char *buf_iova_addr = ring + (size_t)(idx & idx_mask) * TX_DESC_SIZE;
    unsigned char *buf_len_addr = buf_iova_addr + 8;
    unsigned char *desc_flags_addr = buf_len_addr + 2;

    uint16_t flags = rte_read16(desc_flags_addr);
    // Send packets in burst if buffer is full or no more packets are available
    if (nb_mbufs_used == burst_size || !(flags & TX_FLAG_AVAIL)) {
      uint16_t nb_tx = rte_eth_tx_burst(0, queue_idx, mbufs, nb_mbufs_used);
      if (nb_tx < nb_mbufs_used) {
        // Drop packets we couldn't send
        rte_pktmbuf_free_bulk(mbufs + nb_tx, nb_mbufs_used - nb_tx);
      }
      nb_mbufs_used = 0;
      // Flags were possibly changed during tx if buffers were freed.
      flags = rte_read16(desc_flags_addr);
    }

    // If next descriptor is not available, try again
    if (!(flags & TX_FLAG_AVAIL)) {
      continue;
    }

    if constexpr (ZERO_COPY) {
      // If this buffer is attached to an mbuf, we fully wrapped around and need
      // to wait until this descriptor was sent by DPDK.
      if (flags & TX_FLAG_ATTACHED) {
        int freed = rte_eth_tx_done_cleanup(0, queue_idx, 0);
        if constexpr (DEBUG_OUTPUT) {
          nb_cleanup_calls++;
          last_cleanup_result = freed;
        }
        continue;
      }
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

    // Create pktmbuf
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(pool);
    if (!mbuf) {
      // No buffer available
      printf("Vdpdk mbuf alloc failed\n");
      // Try freeing buffers
      int freed = rte_eth_tx_done_cleanup(0, queue_idx, 0);
      if constexpr (DEBUG_OUTPUT) {
        nb_cleanup_calls++;
        last_cleanup_result = freed;
      }
      continue;
    }
    if (rte_pktmbuf_tailroom(mbuf) < buf_len) {
      // Packet too large, drop it
      printf("Packet from VM is too large for buffer.\n");
      rte_pktmbuf_free(mbuf);
    } else {
      if constexpr (ZERO_COPY) {
        // Initialize shared data info and copy descriptor into mbuf
        auto shinfo = (struct rte_mbuf_ext_shared_info *)rte_mbuf_to_priv(mbuf);
        unsigned char *mbuf_desc = (unsigned char *)rte_mbuf_to_priv(mbuf) + sizeof(*shinfo);
        shinfo->free_cb = free_cb;
        shinfo->fcb_opaque = mbuf_desc;
        rte_mbuf_ext_refcnt_set(shinfo, 1);
        memcpy(mbuf_desc + 8, buf_iova_addr + 8, TX_DESC_SIZE - 8);
        // We also need to pass a pointer to the queue data to the free callback
        uintptr_t uptr = (uintptr_t)&queue_data;
        memcpy(mbuf_desc, &uptr, sizeof(uintptr_t));

        // We use IOVA as VA mode, so we can simply pass the buf_addr for buf_iova.
        rte_pktmbuf_attach_extbuf(mbuf, buf_addr, (rte_iova_t)buf_addr, buf_len, shinfo);
      } else {
        // Copy data to mbuf
        rte_memcpy(rte_pktmbuf_mtod(mbuf, void *), buf_addr, buf_len);
      }
      mbuf->data_len = buf_len;
      mbuf->pkt_len = buf_len;
      mbuf->nb_segs = 1;
      mbufs[nb_mbufs_used++] = mbuf;
    }

    if constexpr (ZERO_COPY) {
      // Mark buffer as attached
      flags |= TX_FLAG_ATTACHED;
      // We do not synchronize across threads with this flag,
      // so no memory barrier is needed.
      rte_write16_relaxed(flags, desc_flags_addr);
    } else {
      // Release buffer back to VM
      flags &= ~TX_FLAG_AVAIL;
      rte_write16(flags, desc_flags_addr);
    }

    // Go to next descriptor
    // Index wraps naturally on overflow
    idx++;
  }

  // Try cleaning up to refill mempool
  // TODO: consider simply freeing and allocating a new mempool
  rte_pktmbuf_free_bulk(mbufs, nb_mbufs_used);
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
// Might not be needed after all. But can be repurposed for RX?

// struct vdpdk_tx_mempool_private {
//   // Drivers may expect this as the memory pool's private data.
//   // By making it the first member, any pointer to vdpdk_tx_mempool_private
//   // is also a valid pointer to rte_pktmbuf_pool_private.
//   struct rte_pktmbuf_pool_private pktmbuf_private;

//   struct rte_mempool_ops *inner;
// };

// static int vdpdk_tx_mempool_alloc(struct rte_mempool *mp) {
//   auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
//   return priv->inner->alloc(mp);
// }

// void vdpdk_tx_mempool_free(struct rte_mempool *mp) {
//   auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
//   priv->inner->free(mp);
// }

// static int vdpdk_tx_mempool_enqueue(struct rte_mempool *mp, void *const *obj_table, unsigned int n) {
//   auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
//   return priv->inner->enqueue(mp, obj_table, n);
// }

// static int vdpdk_tx_mempool_dequeue(struct rte_mempool *mp, void **obj_table, unsigned int n) {
//   auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
//   return priv->inner->dequeue(mp, obj_table, n);
// }

// static unsigned vdpdk_tx_mempool_get_count(const struct rte_mempool *mp) {
//   auto priv = (const struct vdpdk_tx_mempool_private *)
//     rte_mempool_get_priv((struct rte_mempool *)mp);
//   return priv->inner->get_count(mp);
// }

// // This memory pool is a wrapper around a regular dpdk memory pool.
// // It exists to detect when a driver frees a pktmbuf. The freed pktmbuf is then
// // handed back to the guest VM.
// struct rte_mempool_ops vdpdk_tx_mempool {
//  .name = "VDPDK_TX_MEMPOOL",
//  .alloc = vdpdk_tx_mempool_alloc,
//  .free = vdpdk_tx_mempool_free,
//  .enqueue = vdpdk_tx_mempool_enqueue,
//  .dequeue = vdpdk_tx_mempool_dequeue,
//  .get_count = vdpdk_tx_mempool_get_count,
// };

// RTE_MEMPOOL_REGISTER_OPS(vdpdk_tx_mempool);

// static void vdpdk_tx_mempool_init(struct rte_mempool *mp,
//                                   void *opaque_arg,
//                                   void *_m, unsigned int i) {
//   auto m = (struct rte_mbuf *)_m;
//   size_t mbuf_size = sizeof(struct rte_mbuf) + TX_DESC_SIZE;
//   memset(m, 0, mbuf_size);
//   m->priv_size = TX_DESC_SIZE;

//   m->pool = mp;
//   m->nb_segs = 1;
//   m->port = RTE_MBUF_PORT_INVALID;
//   m->ol_flags = RTE_MBUF_F_EXTERNAL;
//   rte_mbuf_refcnt_set(m, 1);
//   m->next = NULL;
// }

// static struct rte_mempool *vdpdk_create_tx_mempool(unsigned n) {
//   size_t element_size = sizeof(struct rte_mbuf) + TX_DESC_SIZE;
//   struct rte_mempool *mp = rte_mempool_create_empty("vdpdk_tx_adapter", n, element_size, 64, sizeof(struct vdpdk_tx_mempool_private), rte_socket_id(), 0);
//   if (!mp) {
//     die("Could not allocate vdpdk tx adapter mempool.");
//   }
//   auto best_ops_name = rte_mbuf_best_mempool_ops();
//   struct rte_mempool_ops *best_ops = nullptr;
//   for (unsigned i = 0; i < rte_mempool_ops_table.num_ops; i++) {
//     if (strcmp(best_ops_name, rte_mempool_ops_table.ops[i].name) == 0) {
//       best_ops = &rte_mempool_ops_table.ops[i];
//     }
//   }
//   if (!best_ops) {
//     die("Invalid dpdk mempool ops configured.");
//   }

//   auto priv = (struct vdpdk_tx_mempool_private *)rte_mempool_get_priv(mp);
//   memset(priv, 0, sizeof(*priv));

//   // TODO: pass through buffer sizes
//   priv->pktmbuf_private.mbuf_priv_size = TX_DESC_SIZE;
//   priv->pktmbuf_private.mbuf_data_room_size = 0;
//   // We set this flag as a precaution, to ensure DPDK will not try to manage
//   // the data buffers associated with these mbufs.
//   priv->pktmbuf_private.flags = RTE_PKTMBUF_POOL_F_PINNED_EXT_BUF;
//   priv->inner = best_ops;

//   if (rte_mempool_set_ops_byname(mp, "VDPDK_TX_MEMPOOL", nullptr) != 0) {
//     die("Could not set vdpdk tx mempool adapter ops.");
//   }

//   if (rte_mempool_populate_default(mp) < 0) {
//     die("Could not allocate vdpdk tx adapter mempool.");
//   }
// }
