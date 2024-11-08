#pragma once

#include "src/devices/vmux-device.hpp"
#include "memfd.hpp"
#include <string>
#include <thread>

class VdpdkDevice : public VmuxDevice {
public:
  VdpdkDevice(int device_id, std::shared_ptr<Driver> driver);
  
  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override;

private:
  std::string dbg_string;
  MemFd txbuf;
  MemFd rxbuf;
  
  void rx_callback_fn(int vm_number);
  static void rx_callback_static(int vm_number, void *);

  ssize_t region_access_cb(char *buf, size_t count, loff_t offset, bool is_write);
  static ssize_t region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                         loff_t offset, bool is_write);
  ssize_t region_access_write(char *buf, size_t count, unsigned offset);
  ssize_t region_access_read(char *buf, size_t count, unsigned offset);

  // declare this last, so it is destroyed first
  std::jthread tx_poll_thread;
  void tx_poll(std::stop_token stop);
};
