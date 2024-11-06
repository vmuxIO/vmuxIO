#pragma once

#include "src/devices/vmux-device.hpp"
#include "memfd.hpp"
#include <string>
#include <vector>

class VdpdkDevice : public VmuxDevice {
public:
  VdpdkDevice(int device_id, std::shared_ptr<Driver> driver);
  
  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override;

private:
  std::string dbg_string;
  std::vector<unsigned char> pkt_buf; // TODO remove
  MemFd txbuf{"vdpdk_tx", 0x1000};
  MemFd rxbuf{"vdpdk_rx", 0x1000};
  
  void rx_callback_fn(int vm_number);
  static void rx_callback_static(int vm_number, void *);

  ssize_t region_access_cb(char *buf, size_t count, loff_t offset, bool is_write);
  static ssize_t region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                         loff_t offset, bool is_write);
  ssize_t region_access_write(char *buf, size_t count, unsigned offset);
  ssize_t region_access_read(char *buf, size_t count, unsigned offset);
};
