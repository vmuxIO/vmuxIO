#pragma once
#include <cstddef>
#include <cstdlib>
#include <optional>
#include "util.hpp"

// Abstract class for Driver backends
class Driver {
public:
  static const int MAX_BUF = 9000; // should be enough even for most jumboframes

  int fd = 0; // may be a non-null fd to poll on
  size_t nb_bufs = 0; // rxBufs allocated
  size_t nb_bufs_used = 0; // rxBufs filled with data
  char **rxBufs;
  size_t *rxBuf_used; // how much each rxBuf is actually filled with data
  std::optional<uint16_t> *rxBuf_queue; // optional hints to destination queues
  char txFrame[MAX_BUF];

  void alloc_rx_lists(size_t nb_bufs) {
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
  virtual void send(const char *buf, const size_t len) = 0;
  virtual void recv(int vm_id) = 0;
  virtual void recv_consumed(int vm_id) = 0;
  
  // ptp
  virtual struct timespec readCurrentTimestamp() {           return { .tv_sec=0, .tv_nsec=0 }; };
  virtual struct timespec readTxTimestamp(uint16_t portid) { return { .tv_sec=0, .tv_nsec=0 }; };
  virtual struct timespec readRxTimestamp(uint16_t portid) { return { .tv_sec=0, .tv_nsec=0 }; };

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

};
