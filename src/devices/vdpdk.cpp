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
  // Signal BAR
  DEBUG_STRING = 0x0,
  TX_QUEUE_START = 0x40,
  TX_QUEUE_STOP = 0x80,

  RX_QUEUE_START = 0x140,
  RX_QUEUE_STOP = 0x180,

  FLOW_CREATE = 0x200,
  FLOW_DESTROY = 0x240,

  EVENT_TX = 0x300,

  // TX BAR
  // 0x00 - 0xFF: Reserved for queue setup
  TX_WANT_SIGNAL = 0x100,

  // 0x100 - 0x1FF: intr flags
  RX_WANT_INTR = 0x100, // 0x40 per queue
};

constexpr uint64_t MAX_EMPTY_POLLS = 100000;

VdpdkDevice::VdpdkDevice(int device_id, std::shared_ptr<Driver> driver, const uint8_t (*mac_addr)[6])
: VmuxDevice(device_id, driver, nullptr),
  txCtl{"vdpdk_tx", REGION_SIZE},
  rxCtl{"vdpdk_rx", REGION_SIZE},
  flowbuf{"vdpdk_flow", 0x1000},
  dpdk_driver(std::dynamic_pointer_cast<Dpdk>(driver)) {
  if (!dpdk_driver) {
    die("Using vDPDK without the DPDK backend is not supported.");
  }
  if (rte_eal_iova_mode() != RTE_IOVA_VA) {
    die("vDPDK only supports virtual address IOVA mode. (Try using DPDK with --iova-mode=va)");
  }

  memcpy(this->mac_addr, mac_addr, 6);

  // TODO figure out appropriate IDs
  this->info.pci_vendor_id = 0x1af4; // Red Hat Virtio Devices
  this->info.pci_device_id = 0x7abc; // Unused
  this->info.pci_subsystem_vendor_id = 0;
  this->info.pci_subsystem_id = 0;
  this->info.pci_device_revision_id = 0;
  this->info.pci_class = 2;
  this->info.pci_subclass = 0;
  this->info.pci_revision = 1;

  // Start in signalling mode to prevent polling before queue setup
  rte_write8(1, txCtl.ptr() + TX_WANT_SIGNAL);
  tx_event_active = true;
  tx_signal_counter = 0;

  rx_event_active = true;
  rx_signal_counter = 0;

  // this->rx_callback = rx_callback_static;
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
                             txCtl.size(), NULL,
                             region_flags, NULL, 0,
                             txCtl.fd(), 0);
  if (ret) {
    die("failed to setup BAR1 region (%d)", errno);
  }

  ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR2_REGION_IDX,
                             rxCtl.size(), NULL,
                             region_flags, NULL, 0,
                             rxCtl.fd(), 0);
  if (ret) {
    die("failed to setup BAR2 region (%d)", errno);
  }

  ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR3_REGION_IDX,
                             flowbuf.size(), NULL,
                             region_flags, NULL, 0,
                             flowbuf.fd(), 0);
  if (ret) {
    die("failed to setup BAR3 region (%d)", errno);
  }

  ret = vfu_setup_device_dma(ctx, dma_register_cb_static,
                             dma_unregister_cb_static);
  if (ret) {
    die("failed to setup device dma (%d)", errno);
  }

  ret = vfu_create_ioeventfd(ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
                             tx_event_fd.fd(), EVENT_TX, 8,
                             0, 0, -1, -1);
  if (ret) {
    die("failed to setup tx eventfd");
  }

  ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSIX_IRQ, MAX_RX_QUEUES);
  if (ret) {
    die("failed to setup irqs");
  }
}

void VdpdkDevice::rx_callback_fn(bool dma_invalidated) {
  int vm_number = device_id;
  driver->recv(vm_number);
  bool queue_rcvd[MAX_RX_QUEUES] = {};
  bool any_rcvd = false;

  for (unsigned q_idx = 0; q_idx < driver->max_queues_per_vm; q_idx++) {
    // We delay loading this until we actually know if packets were received
    std::shared_ptr<RxQueue> rxq{};
    size_t ring_size;
    unsigned char *ring;

    auto &driver_rxq = driver->get_rx_queue(vm_number, q_idx);
    for (uint16_t i = 0; i < driver_rxq.nb_bufs_used; i++) {
      // If we reach this point, at least one packet was received
      any_rcvd = true;
      auto &driver_rxBuf = driver_rxq.rxBufs[i];

      // Lock and load rx_queue parameters
      if (!rxq) {
        size_t rx_queues_idx = (size_t)q_idx % MAX_RX_QUEUES;
        rxq = rx_queues[rx_queues_idx].load();
        // If a queue with this index does not exist, fall back to queue 0
        // TODO: Maybe use a smarter mapping? For example in the case of
        // splitting 4 queues onto 2 vDPDK queues.
        if (!rxq) {
          rxq = rx_queues[0].load();
        } else {
          queue_rcvd[rx_queues_idx] = true;
        }
        // If no queue was created, we are done
        if (!rxq) {
          break;
        } else {
          queue_rcvd[0] = true;
        }

        ring_size = ((size_t)rxq->idx_mask + 1) * RX_DESC_SIZE;
        ring = (unsigned char *)vfuServer->dma_local_addr(rxq->ring_iova, ring_size);
        if (!ring) {
          printf("DMA unmapped during RX poll\n");
          break;
        }
      }

      // if (i == 0)
      //   printf("recv: %u pkts on queue %u\n", (unsigned)driver_rxq.nb_bufs_used, q_idx);

      unsigned char *buf_iova_addr = ring + (size_t)(rxq->idx & rxq->idx_mask) * RX_DESC_SIZE;
      unsigned char *buf_len_addr = buf_iova_addr + 8;
      unsigned char *desc_flags_addr = buf_len_addr + 2;

      uint16_t flags = rte_read16(desc_flags_addr);
      // If next descriptor is not available, we are out of buffers
      if (!(flags & RX_FLAG_AVAIL)) {
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
        break;
      }

      // Check sizes
      size_t pkt_len = driver_rxBuf.used;
      uint16_t pkt_len_u16 = pkt_len;
      if (pkt_len > buf_len || pkt_len > pkt_len_u16) {
        printf("Packet too large (%lx > %x)\n", (unsigned long)pkt_len, (unsigned)buf_len);
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

  // Send guest interrupts
  {
    std::unique_lock vfu_lock{this->vfu_ctx_mutex, std::defer_lock};
    for (unsigned i = 0; i < MAX_RX_QUEUES; i++) {
      if (!queue_rcvd[i]) continue;
      if (!rte_read8(rxCtl.ptr() + RX_WANT_INTR + 0x40 * i)) continue;
      if (!vfu_lock.owns_lock())
        vfu_lock.lock();
      vfu_irq_trigger(this->vfuServer->vfu_ctx, i);
    }
  }

  //  Enable/disable host dpdk interrupts
  if (any_rcvd) {
    rx_event_active = false;
    rx_signal_counter = MAX_EMPTY_POLLS;
  } else {
    if (rx_signal_counter > 0) {
      rx_signal_counter--;
    }
    if (rx_signal_counter == 0) {
      rx_event_active = true;
    }
  }
}

// void VdpdkDevice::rx_callback_static(int vm_number, void *this__) {
//   VdpdkDevice *this_ = (VdpdkDevice *)this__;
//   this_->rx_callback_fn(vm_number);
// }

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

    case TX_QUEUE_STOP: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx != 0) {
        printf("TX_QUEUE_STOP: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      // Stop polling
      tx_queue = nullptr;
      return count;
    }

    case RX_QUEUE_STOP: {
      if (count != 2) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, buf, 2);
      if (queue_idx >= MAX_RX_QUEUES) {
        printf("RX_QUEUE_STOP: Invalid queue idx %d", (int)queue_idx);
        return count;
      }

      rx_queues[queue_idx] = nullptr;
      return count;
    }

    case FLOW_DESTROY: {
      if (count != 8) return -1;
      uint64_t handle;
      memcpy(&handle, buf, 8);

      // TODO: actually disable rule
      return count;
    }

    // Fall-back if ioeventfd is not supported
    case EVENT_TX: {
      tx_event_fd.signal();
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

  switch (offset) {
    case TX_QUEUE_START: {
      if (count != 1) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, txCtl.ptr() + 10, 2);
      if (queue_idx != 0) {
        printf("TX_QUEUE_START: Invalid queue idx %d", (int)queue_idx);
        *buf = 1;
        return count;
      }

      uint64_t ring_addr;
      memcpy(&ring_addr, txCtl.ptr(), 8);
      uint16_t idx_mask;
      memcpy(&idx_mask, txCtl.ptr() + 8, 2);

      printf("TX_QUEUE_START: idx: %d, ring_addr: %llx, mask: %x\n",
             (int)queue_idx, (unsigned long long)ring_addr, (unsigned)idx_mask);

      auto txq = std::make_shared<TxQueue>();
      txq->ring_iova = ring_addr;
      txq->idx_mask = idx_mask;
      txq->front_idx = 0;
      txq->back_idx = 0;
      txq->ring = NULL;

      tx_queue = txq;

      *buf = 0;
      return count;
    }

    case RX_QUEUE_START: {
      if (count != 1) return -1;
      uint16_t queue_idx;
      memcpy(&queue_idx, rxCtl.ptr() + 10, 2);
      if (queue_idx >= MAX_RX_QUEUES) {
        printf("RX_QUEUE_START: Invalid queue idx %d", (int)queue_idx);
        *buf = 1;
        return count;
      }

      uint64_t ring_addr;
      memcpy(&ring_addr, rxCtl.ptr(), 8);
      uint16_t idx_mask;
      memcpy(&idx_mask, rxCtl.ptr() + 8, 2);

      printf("RX_QUEUE_START: idx: %d, ring_addr: %llx, mask: %x\n",
             (int)queue_idx, (unsigned long long)ring_addr, (unsigned)idx_mask);

      auto rxq = std::make_shared<RxQueue>();
      rxq->ring_iova = ring_addr;
      rxq->idx_mask = idx_mask;
      rxq->idx = 0;

      rx_queues[queue_idx] = rxq;

      *buf = 0;
      return count;
    }

    case FLOW_CREATE: {
      if (count != 8) return -1;

      unsigned char *src = flowbuf.ptr();
      // Skip attr for now
      src += sizeof(struct rte_flow_attr);
      uint8_t null_flags = *src++;

      struct rte_flow_item_eth spec = {};
      if (null_flags & 1) {
        memcpy(&spec, src, sizeof(spec));
        src += sizeof(spec);
      }

      struct rte_flow_item_eth last = {};
      if (null_flags & 2) {
        memcpy(&last, src, sizeof(last));
        src += sizeof(last);
      }

      struct rte_flow_item_eth mask = {};
      if (null_flags & 4) {
        memcpy(&mask, src, sizeof(mask));
        src += sizeof(mask);
      }

      null_flags = *src++;
      struct rte_flow_action_queue action = {};
      if (null_flags) {
        memcpy(&action, src, sizeof(action));
        src += sizeof(action);
      }

      // TODO: actually look at structs to support more complex rules

      if (mask.type != 0xFFFF) {
        printf("Invalid eth_flow type mask: %u\n", (unsigned)mask.type);
        uint64_t ret = -1;
        memcpy(buf, &ret, 8);
        return count;
      }

      if (action.index >= 4) {
        printf("Invalid queue index: %u\n", (unsigned)action.index);
        uint64_t ret = -1;
        memcpy(buf, &ret, 8);
        return count;
      }

      uint16_t etype = rte_be_to_cpu_16(spec.type);
      // Don't need handles for now, because we cannot remove rules, so return dummy value
      uint64_t ret =
        driver->add_switch_rule(device_id, mac_addr, etype, action.index)
        ? 0 : -1;
      memcpy(buf, &ret, 8);

      return count;
    }
  }

  printf("Invalid read offset: %x\n", offset);
  return -1;
}

void VdpdkDevice::tx_poll(bool dma_invalidated) {
  constexpr bool DEBUG_OUTPUT = false;
  constexpr bool ZERO_COPY = false;

  std::shared_ptr<TxQueue> queue_data = tx_queue.load();
  if (!queue_data) {
    return;
  }

  uint16_t &idx = queue_data->back_idx;
  const uint16_t &idx_mask = queue_data->idx_mask;

  size_t ring_size = ((size_t)idx_mask + 1) * TX_DESC_SIZE;

  if (dma_invalidated || !queue_data->ring) {
    unsigned char *ring = (unsigned char *)vfuServer->dma_local_addr(queue_data->ring_iova, ring_size);
    queue_data->ring = ring;
    if (!ring) {
      printf("Invalid ring_iova\n");
      tx_queue = nullptr;
      return;
    }
  }

  constexpr unsigned debug_interval = 10000000;
  thread_local unsigned debug_counter = debug_interval;
  thread_local unsigned nb_cleanup_calls = 0;
  thread_local int last_cleanup_result = 0xFFFFFF;

  // Check if we want to enable TX signalling
  // We do this before polling to avoid a race condition when the guest
  // transmits a packet right before we enable signalling.
  if (tx_signal_counter == 0 && !tx_event_active) {
    if constexpr (DEBUG_OUTPUT) {
      printf("TX enable signalling\n");
      // Force debug output on this loops
      debug_counter = 1;
    }
    rte_write8(1, txCtl.ptr() + TX_WANT_SIGNAL);
    tx_event_active = true;
  }

  uint16_t queue_idx = dpdk_driver->get_tx_queue_id(device_id, 0);
  struct rte_mempool *pool = dpdk_driver->tx_mbuf_pools[queue_idx];
  // assert(rte_pktmbuf_priv_size(pool) >= sizeof(struct rte_mbuf_ext_shared_info) + TX_DESC_SIZE);

  constexpr unsigned burst_size = 128;
  struct rte_mbuf *mbufs[burst_size];
  unsigned nb_mbufs_used = 0;

  rte_mbuf_extbuf_free_callback_t free_cb;
  if constexpr (ZERO_COPY) {
    free_cb = [](void *addr, void *opaque) {
      // opaque points to the copied descriptor, with the dma address replaced
      // with a pointer to the queue data;
      uintptr_t uptr;
      TxQueue *queue_data;
      memcpy(&uptr, opaque, sizeof(uptr));
      queue_data = (TxQueue *)uptr;

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
  }

  while (nb_mbufs_used < burst_size) {
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
            auto flags_addr = queue_data->ring + (size_t)idx * TX_DESC_SIZE + 10;
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
        printf("front: %x, back %x\n", queue_data->front_idx, queue_data->back_idx);

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

    unsigned char *buf_iova_addr = queue_data->ring + (size_t)(idx & idx_mask) * TX_DESC_SIZE;
    unsigned char *buf_len_addr = buf_iova_addr + 8;
    unsigned char *desc_flags_addr = buf_len_addr + 2;

    // If next descriptor is not available, try again
    uint16_t flags = rte_read16(desc_flags_addr);
    if (!(flags & TX_FLAG_AVAIL)) {
      break;
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
        break;
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
      tx_queue = nullptr;
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
      break;
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
        uintptr_t uptr = (uintptr_t)queue_data.get();
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

  // Send packets in burst if buffer is full or no more packets are available
  if (nb_mbufs_used > 0) {
    uint16_t nb_tx = rte_eth_tx_burst(0, queue_idx, mbufs, nb_mbufs_used);
    if (nb_tx < nb_mbufs_used) {
      // Drop packets we couldn't send
      rte_pktmbuf_free_bulk(mbufs + nb_tx, nb_mbufs_used - nb_tx);
    }
    // Disable signalling, because packets were transmitted
    if (tx_event_active) {
      if constexpr (DEBUG_OUTPUT) {
        printf("TX disable signalling\n");
      }
      rte_write8(0, txCtl.ptr() + TX_WANT_SIGNAL);
      tx_event_active = false;
    }
    tx_signal_counter = MAX_EMPTY_POLLS;
  } else {
    // No packets were transmitted, reduce counter to eventually enter signalling mode
    if (tx_signal_counter > 0) {
      tx_signal_counter--;
    }
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

void VdpdkThreads::tx_poll_thread_single(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev) {
  Epoll event_waiter;
  event_waiter.add(dev->tx_event_fd);

  std::shared_lock dma_lock(dev->dma_mutex);

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
    }

    bool dma_invalidated = false;

    // Wait if TX is in signalling mode
    if (dev->tx_event_active) {
      dma_lock.unlock();

      event_waiter.wait(1000);
      dev->tx_event_fd.reset();

      dma_lock.lock();
      dma_invalidated = true;
    }

    // Check if DMA mapping wants to change
    if (dev->dma_flag.test()) {
      // Release lock
      dma_lock.unlock();
      // Wait until vfio-user thread holds mutex
      while (dev->dma_flag.test());
      // Re-aquire lock
      dma_lock.lock();

      // Tell vdpdk to look-up addresses again
      dma_invalidated = true;
    }

    dev->tx_poll(dma_invalidated);
  }
}

void VdpdkThreads::rx_poll_thread_single(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev) {
  Epoll intr_waiter;
  int vm_id = dev->device_id;
  dev->dpdk_driver->add_rx_epoll(vm_id, intr_waiter);
  std::shared_lock dma_lock(dev->dma_mutex);
  bool dma_invalidated = false;

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
    }

    // Enable RX interrupts before polling one last time
    bool rx_intr_enabled = false;
    if (dev->rx_event_active) {
      dev->dpdk_driver->enable_rx_intr(vm_id);
      rx_intr_enabled = true;
    }

    // Check if DMA mapping wants to change
    if (dev->dma_flag.test()) {
      // Release lock
      dma_lock.unlock();
      // Wait until vfio-user thread holds mutex
      while (dev->dma_flag.test());
      // Re-aquire lock
      dma_lock.lock();

      // Tell vdpdk to look-up addresses again
      dma_invalidated = true;
    }

    dev->rx_callback_fn(dma_invalidated);
    dma_invalidated = false;

    if (rx_intr_enabled) {
      if (dev->rx_event_active) {
        // If RX interrupts are enabled and dev still wants signalling RX, wait
        dma_lock.unlock();
        intr_waiter.wait(1000);
        dma_lock.lock();
        dma_invalidated = true;
      }
      // No matter if we waited or not, disable interrupts here
      dev->dpdk_driver->disable_rx_intr(vm_id);
    }
  }
}

void VdpdkThreads::tx_poll_thread_multi(std::stop_token stop, std::vector<std::shared_ptr<VdpdkDevice>> devs) {
  Epoll event_waiter;
  for (const auto &dev : devs) {
    event_waiter.add(dev->tx_event_fd);
  }

  std::vector<std::shared_lock<std::shared_mutex>> dma_locks;
  dma_locks.reserve(devs.size());
  for (auto &dev : devs) {
    dma_locks.emplace_back(dev->dma_mutex);
  }

  std::vector<bool> dma_invalidated_flags(devs.size(), false);
  bool eventing_active = false;

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
    }

    // Wait if TX is in signalling mode
    if (eventing_active) {
      for (auto &dma_lock : dma_locks) {
        dma_lock.unlock();
      }

      event_waiter.wait(1000);
      for (auto &dev : devs) {
        dev->tx_event_fd.reset();
      }

      for (auto &dma_lock : dma_locks) {
        dma_lock.lock();
      }
      for (auto &&dma_invalidated : dma_invalidated_flags) {
        dma_invalidated = true;
      }
    }

    eventing_active = true;
    for (size_t i = 0; i < devs.size(); i++) {
      auto &dev = devs[i];
      // Check if DMA mapping wants to change
      if (dev->dma_flag.test()) {
        // Release lock
        dma_locks[i].unlock();
        // Wait until vfio-user thread holds mutex
        while (dev->dma_flag.test());
        // Re-aquire lock
        dma_locks[i].lock();

        // Tell vdpdk to look-up addresses again
        dma_invalidated_flags[i] = true;
      }

      dev->tx_poll(dma_invalidated_flags[i]);
      dma_invalidated_flags[i] = false;
      if (!dev->tx_event_active) eventing_active = false;
    }
  }
}

void VdpdkThreads::rx_poll_thread_multi(std::stop_token stop, std::vector<std::shared_ptr<VdpdkDevice>> devs) {
  Epoll intr_waiter;
  for (auto &dev : devs) {
    dev->dpdk_driver->add_rx_epoll(dev->device_id, intr_waiter);
  }

  std::vector<std::shared_lock<std::shared_mutex>> dma_locks;
  dma_locks.reserve(devs.size());
  for (auto &dev : devs) {
    dma_locks.emplace_back(dev->dma_mutex);
  }

  std::vector<bool> dma_invalidated_flags(devs.size(), false);
  bool eventing_active = false;

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
    }

    bool rx_intr_enabled = false;
    if (eventing_active) {
      for (auto &dev : devs) {
        dev->dpdk_driver->enable_rx_intr(dev->device_id);
      }
      rx_intr_enabled = true;
    }

    eventing_active = true;
    for (size_t i = 0; i < devs.size(); i++) {
      auto &dev = devs[i];
      // Check if DMA mapping wants to change
      if (dev->dma_flag.test()) {
        // Release lock
        dma_locks[i].unlock();
        // Wait until vfio-user thread holds mutex
        while (dev->dma_flag.test());
        // Re-aquire lock
        dma_locks[i].lock();

        // Tell vdpdk to look-up addresses again
        dma_invalidated_flags[i] = true;
      }

      dev->rx_callback_fn(dma_invalidated_flags[i]);
      dma_invalidated_flags[i] = false;
      if (!dev->rx_event_active) eventing_active = false;
    }

    if (rx_intr_enabled) {
      if (eventing_active) {
        for (auto &dma_lock : dma_locks) {
          dma_lock.unlock();
        }
        intr_waiter.wait(1000);
        for (auto &dma_lock : dma_locks) {
          dma_lock.lock();
        }
        for (auto &&dma_invalidated : dma_invalidated_flags) {
          dma_invalidated = true;
        }
      }
      for (auto &dev : devs) {
        dev->dpdk_driver->disable_rx_intr(dev->device_id);
      }
    }
  }
}

void VdpdkThreads::rxtx_poll_thread_multi(std::stop_token stop, std::vector<std::shared_ptr<VdpdkDevice>> devs) {
  Epoll event_waiter;
  for (auto &dev : devs) {
    dev->dpdk_driver->add_rx_epoll(dev->device_id, event_waiter);
    event_waiter.add(dev->tx_event_fd);
  }

  std::vector<std::shared_lock<std::shared_mutex>> dma_locks;
  dma_locks.reserve(devs.size());
  for (auto &dev : devs) {
    dma_locks.emplace_back(dev->dma_mutex);
  }

  std::vector<bool> dma_invalidated_flags(devs.size(), false);
  bool eventing_active = false;

  while (true) {
    // Check if stop requested
    if (stop.stop_requested()) {
      break;
    }

    bool rx_intr_enabled = false;
    if (eventing_active) {
      for (auto &dev : devs) {
        dev->dpdk_driver->enable_rx_intr(dev->device_id);
      }
      rx_intr_enabled = true;
    }

    eventing_active = true;
    for (size_t i = 0; i < devs.size(); i++) {
      auto &dev = devs[i];
      // Check if DMA mapping wants to change
      if (dev->dma_flag.test()) {
        // Release lock
        dma_locks[i].unlock();
        // Wait until vfio-user thread holds mutex
        while (dev->dma_flag.test());
        // Re-aquire lock
        dma_locks[i].lock();

        // Tell vdpdk to look-up addresses again
        dma_invalidated_flags[i] = true;
      }

      dev->rx_callback_fn(dma_invalidated_flags[i]);
      dev->tx_poll(dma_invalidated_flags[i]);
      dma_invalidated_flags[i] = false;
      if (!dev->rx_event_active || !dev->tx_event_active) eventing_active = false;
    }

    if (eventing_active) {
      for (auto &dma_lock : dma_locks) {
        dma_lock.unlock();
      }

      event_waiter.wait(1000);
      for (auto &dev : devs) {
        dev->tx_event_fd.reset();
      }

      for (auto &dma_lock : dma_locks) {
        dma_lock.lock();
      }
      for (auto &&dma_invalidated : dma_invalidated_flags) {
        dma_invalidated = true;
      }
    }
    if (rx_intr_enabled) {
      for (auto &dev : devs) {
        dev->dpdk_driver->disable_rx_intr(dev->device_id);
      }
    }
  }
}

void VdpdkThreads::add_device(std::shared_ptr<VdpdkDevice> dev, cpu_set_t rx_pin, cpu_set_t tx_pin, cpu_set_t vm_cluster) {
  Info info {
    .dev = std::move(dev),
    .rx_pin = rx_pin,
    .tx_pin = tx_pin,
  };
  for (auto &cluster : clusters) {
    if (CPU_EQUAL(&vm_cluster, &cluster.vm_cluster)) {
      cluster.devs.push_back(std::move(info));
      return;
    }
  }
  Cluster cluster {
    .vm_cluster = vm_cluster,
    .devs = {std::move(info)},
  };
  clusters.push_back(std::move(cluster));
}

static void pin_thread(std::jthread &jt, const char *name, cpu_set_t set) {
  pthread_t t = jt.native_handle();
  pthread_setname_np(t, name);
  int ret = pthread_setaffinity_np(t, sizeof(set), &set);
  if (ret != 0) {
    die("failed to set pthread cpu affinity");
  }
}

static std::string fmt_thread_name(const char *prefix, std::span<std::shared_ptr<VdpdkDevice>> devs) {
  std::string result{prefix};
  for (size_t i = 0; i < devs.size(); i++) {
    if (i != 0) result.push_back('_');
    std::format_to(std::back_inserter(result), "{}", devs[i]->device_id);
  }
  return result;
}

void VdpdkThreads::start() {
  for (const auto &cluster: clusters) {
    // If exactly one VM in cluster
    if (cluster.devs.size() == 1) {
      // Single VM per thread
      const auto &info = cluster.devs[0];
      auto dev = info.dev;
      std::jthread rxthread{[dev](std::stop_token stop) {
        rx_poll_thread_single(stop, dev);
      }};
      std::jthread txthread{[dev](std::stop_token stop) {
        tx_poll_thread_single(stop, dev);
      }};

      pin_thread(rxthread, std::format("vdpdkRx{}", info.dev->device_id).c_str(), info.rx_pin);
      pin_thread(txthread, std::format("vdpdkTx{}", info.dev->device_id).c_str(), info.tx_pin);

      threads.push_back(std::move(rxthread));
      threads.push_back(std::move(txthread));

      continue;
    }

    std::vector<std::shared_ptr<VdpdkDevice>> devs;
    for (auto &dev : cluster.devs) {
      devs.push_back(dev.dev);
    }

    int approx_free_cpus = CPU_COUNT(&cluster.vm_cluster) - (int)cluster.devs.size();
    // If we're running out of CPUs
    if (approx_free_cpus < 2) {
      // Poll RX and TX on single thread for multiple VMs
      std::jthread rxtxthread{[devs](std::stop_token stop) {
        rxtx_poll_thread_multi(stop, std::move(devs));
      }};

      pin_thread(rxtxthread, fmt_thread_name("vdpdkRxTx", devs).c_str(), cluster.vm_cluster);

      threads.push_back(std::move(rxtxthread));
      continue;
    }

    // Create single RX and TX thread for all VMs in this cluster
    std::jthread rxthread{[devs](std::stop_token stop) {
      rx_poll_thread_multi(stop, std::move(devs));
    }};
    std::jthread txthread{[devs](std::stop_token stop) {
      tx_poll_thread_multi(stop, std::move(devs));
    }};

    pin_thread(rxthread, fmt_thread_name("vdpdkRx", devs).c_str(), cluster.vm_cluster);
    pin_thread(txthread, fmt_thread_name("vdpdkTx", devs).c_str(), cluster.vm_cluster);

    threads.push_back(std::move(rxthread));
    threads.push_back(std::move(txthread));
  }
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
