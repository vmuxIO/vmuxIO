#pragma once

#include "devices/vdpdk-consts.hpp"
#include "drivers/dpdk.hpp"
#include "src/devices/vmux-device.hpp"
#include "memfd.hpp"
#include <atomic>
#include <vector>
#include <shared_mutex>
#include <string>
#include <thread>

class VdpdkDevice : public VmuxDevice {
public:
  VdpdkDevice(int device_id, std::shared_ptr<Driver> driver, const uint8_t (*mac_addr)[6]);

  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override;

private:
  std::string dbg_string;
  MemFd txCtl;
  MemFd rxCtl;
  MemFd flowbuf;

  std::shared_ptr<Dpdk> dpdk_driver;

  uint8_t mac_addr[6];

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
  std::array<
    std::atomic<std::shared_ptr<RxQueue>>,
    VDPDK_CONSTS::MAX_RX_QUEUES
  > rx_queues;

  struct TxQueue {
    uintptr_t ring_iova;
    unsigned char *ring;
    uint16_t idx_mask;
    uint16_t front_idx, back_idx;
  };
  std::atomic<std::shared_ptr<TxQueue>> tx_queue;

  void rx_callback_fn(bool dma_invalidated);
  // static void rx_callback_static(int vm_number, void *);

  ssize_t region_access_cb(char *buf, size_t count, loff_t offset, bool is_write);
  static ssize_t region_access_cb_static(vfu_ctx_t *ctx, char *buf, size_t count,
                                         loff_t offset, bool is_write);
  ssize_t region_access_write(char *buf, size_t count, unsigned offset);
  ssize_t region_access_read(char *buf, size_t count, unsigned offset);

  void dma_register_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  static void dma_register_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  void dma_unregister_cb(vfu_ctx_t *ctx, vfu_dma_info_t *info);
  static void dma_unregister_cb_static(vfu_ctx_t *ctx, vfu_dma_info_t *info);

  void tx_poll(bool dma_invalidated);

  friend class VdpdkThreads;
};

class VdpdkThreads {
public:
  explicit VdpdkThreads(size_t sharing_thresh);
  void add_device(std::shared_ptr<VdpdkDevice>, cpu_set_t rx_pin, cpu_set_t tx_pin);
  void start();

private:
  size_t sharing_thresh;
  struct Info {
    std::shared_ptr<VdpdkDevice> dev;
    cpu_set_t rx_pin;
    cpu_set_t tx_pin;
  };
  std::vector<Info> start_info;
  std::vector<std::jthread> threads;

  static void tx_poll_thread_single(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev);
  static void rx_poll_thread_single(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev);
  static void tx_poll_thread_double(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev1, std::shared_ptr<VdpdkDevice> dev2);
  static void rx_poll_thread_double(std::stop_token stop, std::shared_ptr<VdpdkDevice> dev1, std::shared_ptr<VdpdkDevice> dev2);
};
