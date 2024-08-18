#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include "util.hpp"

struct vmux_descriptor {
  char *buf;
  size_t len; // used size of buffer
  std::optional<uint16_t> dst_queue;
};

// Abstract class for Driver backends
class Driver {
public:
  static const int MAX_BUF = 9000; // should be enough even for most jumboframes

  int fd = 0; // may be a non-null fd to poll on
  size_t nb_bufs = 0; // rxBufs allocated
  // struct vmux_descriptor **bufs; //
  // TODO revise this: !!!
  size_t *nb_bufs_used; // rxBufs filled with data (per queue, value must be <= BURST_SIZE) (size=global queues)
  char **rxBufs; // size=global_queues * BUSRT_SIZE
  size_t *rxBuf_used; // how much each rxBuf is actually filled with data (size=global_queues * BURST_SIZE)
  std::optional<uint16_t> *rxBuf_queue; // optional hints to destination queues (size=global_queues * BURST_SIZE)
  char txFrame[MAX_BUF];

  void alloc_rx_lists(size_t global_queues, size_t per_queue_bursts) {
    this->nb_bufs_used = (size_t*) calloc(global_queues, sizeof(size_t));
    size_t nb_bufs = global_queues * per_queue_bursts;
    this->nb_bufs = nb_bufs;
    this->rxBufs = (char**) malloc(nb_bufs * sizeof(char*));
    this->rxBuf_used = (size_t*) calloc(nb_bufs, sizeof(size_t));
    this->rxBuf_queue = (std::optional<uint16_t>*) malloc(nb_bufs * sizeof(std::optional<uint16_t>));
    this->rxBuf_queue = new std::optional<uint16_t>[nb_bufs]();
    if (!this->rxBufs)
      die("Cannot allocate rxBufs");
  }

  void alloc_rx_bufs() {
    if (this->nb_bufs == 0)
      die("rxBuf lists uninitialized. Call alloc_rx_lists first.")
    for (size_t i = 0; i < nb_bufs; i++) {
      this->rxBufs[i] = (char*) malloc(this->MAX_BUF);
      if (!this->rxBufs)
        die("Cannot allocate rxBuf");
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
