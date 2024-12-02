#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>
#include "util.hpp"

struct vmux_descriptor {
  char *buf;
  size_t len; // used size of buffer
  std::optional<uint16_t> dst_queue;
};

// Abstract class for Driver backends
class Driver {
public:
  static constexpr int MAX_BUF = 9000; // should be enough even for most jumboframes

  int fd = 0; // may be a non-null fd to poll on

  struct RxBuf {
    char *data = nullptr;
    size_t used = 0; // how much the rxBuf is actually filled with data
    std::optional<uint16_t> queue; // optional hint to destination queue
  };

  struct RxQueue {
    std::vector<RxBuf> rxBufs;
    size_t nb_bufs_used = 0; // rxBufs filled with data
  };

  std::vector<RxQueue> rxQueues;

  // to map VMs to RxQueues:
  // for every i from 0 to below max_queues_per_vm:
  // the queue index `vm_queue_stride * vm_id + i` belongs to VM
  unsigned max_queues_per_vm;
  unsigned vm_queue_stride;

  RxQueue &get_rx_queue(unsigned vm, unsigned queue) {
    return rxQueues[vm * vm_queue_stride + queue];
  }

  void alloc_rx_lists(size_t global_queues,
                      size_t per_queue_bursts,
                      unsigned max_queues_per_vm,
                      unsigned vm_queue_stride) {
    rxQueues.resize(global_queues);
    for (auto &rxq : rxQueues) {
      rxq.rxBufs.resize(per_queue_bursts);
    }
    this->max_queues_per_vm = max_queues_per_vm;
    this->vm_queue_stride = vm_queue_stride;
  }

  void alloc_rx_bufs() {
    if (rxQueues.empty())
      die("rxBuf lists uninitialized. Call alloc_rx_lists first.")

    for (auto &rxq : rxQueues) {
      for (auto &rxbuf : rxq.rxBufs) {
        rxbuf.data = (char *)malloc(Driver::MAX_BUF);
        if (!rxbuf.data)
          die("Cannot allocate rxBuf");
      }
    }
  }

  void free_rx_bufs() {
    for (auto &rxq : rxQueues) {
      for (auto &rxbuf : rxq.rxBufs) {
        if (!rxbuf.data)
          continue;
        free(rxbuf.data);
        rxbuf.data = nullptr;
      }
    }
  }

  // vm_id can be used to serve multiple VMs with one single driver
  virtual void send(int vm_id, const char *buf, const size_t len) = 0;
  // specialized function to send packets with TSO
  // can be called multiple times to collect multiple buffers of data
  // set end_of_packet=true on the last call
  // if this function returns false at any moment, no data was sent
  // packets were queued for sending if this function returns true while end_of_packet==true
  virtual bool send_tso(int vm_id, const char *buf, const size_t len,
                        const bool end_of_packet, uint64_t l2_len,
                        uint64_t l3_len, uint64_t l4_len, uint64_t tso_segsz) {
    // by default, TSO is not supported
    return false;
  }
  virtual void recv(int vm_id) = 0;
  virtual void recv_consumed(int vm_id) = 0;
  
  // PTP
  virtual void enableTimesync(uint16_t port) {};
  virtual struct timespec readCurrentTimestamp() { return { .tv_sec=0, .tv_nsec=0 }; };
  virtual uint64_t readTxTimestamp(uint16_t portid) { return 0; };
  virtual uint64_t readRxTimestamp(uint16_t portid) { return 0; };

  // return false if rule cant be allocated
  virtual bool add_switch_rule(int vm_id, uint8_t mac_addr[6], uint16_t dst_queue) {
    return false;
  }

  virtual bool mediation_enable(int vm_id) {
    return false;
  }

  virtual bool mediation_disable(int vm_id) {
    return false;
  }
  
  virtual bool is_mediating(int vm_id) {
    return false;
  }
};
