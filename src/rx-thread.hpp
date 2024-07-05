#pragma once

#include "devices/vmux-device.hpp"
#include "util.hpp"
#include <atomic>
#include <thread>

/**
 * Does busy polling on the VmuxDevices rx_callback (should probably only be used with DPDK drivers).
 */
class RxThread {
  public:
    std::thread runner;
    std::atomic_bool running; // set to false to terminate this thread
    std::string termination_error; // non-null if Runner terminated with error
    std::shared_ptr<VmuxDevice> device;

    RxThread(std::shared_ptr<VmuxDevice> device): device(device) { }

    void start() {
      running.store(1);
      runner = std::thread(&RxThread::run, this);
      std::string name = std::string("vmux-RxThread") + std::to_string(device->device_id);
      pthread_setname_np(runner.native_handle(), name.c_str());
    }

    void stop() { running.store(0); }

    Result<void> join() {
      runner.join();
      if (!this->termination_error.empty()) {
        return Err(this->termination_error);
      }
      return Ok();
    }

  private:
    void run() {
      while (running.load()) {
        // dpdk: do busy polling
        device->rx_callback(device->device_id, device.get());
      }
    }
};
