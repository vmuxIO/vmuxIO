#pragma once

#include "src/devices/vmux-device.hpp"

class VdpdkDevice : public VmuxDevice {
public:
  VdpdkDevice(int device_id, std::shared_ptr<Driver> driver);
  
  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override;

private:
  void rx_callback_fn(int vm_number);
  static void rx_callback_static(int vm_number, void *);
};
