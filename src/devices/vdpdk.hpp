#pragma once

#include "src/devices/vmux-device.hpp"
#include <memory>

class VdpdkDevice : public VmuxDevice {
public:
  VdpdkDevice(int device_id, std::shared_ptr<Driver> driver);
  
  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override;

private:
  std::unique_ptr<char[]> recv_memory;
  
  void rx_callback_fn(int vm_number);
  static void rx_callback_static(int vm_number, void *);

  ssize_t region_access_cb(char *buf, size_t count, loff_t offset, bool is_write);
  static ssize_t region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                         loff_t offset, bool is_write);
};
