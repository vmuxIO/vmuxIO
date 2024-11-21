#pragma once

#include "src/devices/vmux-device.hpp"
#include "memfd.hpp"
#include <atomic>
#include <shared_mutex>
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

  // we use this to ensure we don't have to lock the VfuServer for every dma access
  // if we don't lock at all, it's possible that the dma mapping is released while we access it
  std::shared_mutex dma_mutex;
  // set if vfio-user wants to change dma mapping
  std::atomic_flag dma_flag;

  struct RxQueue {
    uintptr_t ring_iova;
    uint16_t idx_mask;
    uint16_t idx;
  };
  std::atomic<std::shared_ptr<RxQueue>> rx_queue;

  void rx_callback_fn(int vm_number);
  static void rx_callback_static(int vm_number, void *);

  ssize_t region_access_cb(char *buf, size_t count, loff_t offset, bool is_write);
  static ssize_t region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                         loff_t offset, bool is_write);
  ssize_t region_access_write(char *buf, size_t count, unsigned offset);
  ssize_t region_access_read(char *buf, size_t count, unsigned offset);

  void dma_register_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  static void dma_register_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  void dma_unregister_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  static void dma_unregister_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info);

  // declare this last, so it is destroyed first
  std::jthread tx_poll_thread;
  void tx_poll(std::stop_token stop, uintptr_t ring_iova, uint16_t idx_mask);
};
