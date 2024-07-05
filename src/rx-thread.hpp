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
      pthread_t thread = runner.native_handle();

      // set name
      char name[16] = { 0 };
      snprintf(name, 16, "vmuxRx%u", device->device_id);
      int ret = pthread_setname_np(thread, name);
      if (ret != 0) {
        die("cant rename thread");
      }

      // set cpu affinity
      size_t num_cores = sysconf(_SC_NPROCESSORS_ONLN); // Get the number of available cores
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(((device->device_id * 6) + 7) % num_cores, &cpuset);
      // for (size_t j = 0; j < num_cores; j++)
      //     CPU_SET(j, &cpuset);
      ret = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
      if (ret != 0)
          die("failed to set pthread cpu affinity");
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
