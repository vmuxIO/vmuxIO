#pragma once

#include "interrupts/none.hpp"
#include "interrupts/simbricks.hpp"
#include "libsimbricks/simbricks/nicbm/nicbm.h"
#include "libvfio-user.h"
#include "sims/nic/e810_bm/e810_bm.h"
#include "src/devices/vmux-device.hpp"
#include "util.hpp"
#include "vfio-consumer.hpp"
#include "vfio-server.hpp"
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <ctime>

#define NUM_MSIX_IRQs 16 // choose small to avoid unneccessary polling in processAllPollTimers

class E810EmulatedDevice : public VmuxDevice, public std::enable_shared_from_this<E810EmulatedDevice> {
  static const unsigned BAR_REGS = 0;
private:
  /** we don't pass the entire device to the model, but only the callback
   * adaptor. We choose this approach, because we don't want to purpose
   * build the device to fit the simbricks code, and don't want to change
   * the simbricks code too much to fit the vmux code. **/
  std::shared_ptr<nicbm::Runner::CallbackAdaptor> callbacks;

  SimbricksProtoPcieDevIntro deviceIntro = SimbricksProtoPcieDevIntro();

  const uint8_t mac_addr[6] = {};

  epoll_callback tapCallback;
  int efd = 0; // if non-null: eventfd registered for this->tap->fd
               
  std::vector<std::shared_ptr<InterruptThrottlerSimbricks>> irqThrottle;

  void registerDriverEpoll(std::shared_ptr<Driver> driver, int efd) {
    if (driver->fd == 0)
      return;
      // die("E1000 only supports drivers that offer fds to wait on")

    this->tapCallback.fd = driver->fd;
    this->tapCallback.callback = E810EmulatedDevice::driver_cb;
    this->tapCallback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->tapCallback;

    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, driver->fd, &e))
      die("could not register driver fd to epoll");

    this->efd = efd;
  }

public:
  std::shared_ptr<i40e::e810_bm> model;

  E810EmulatedDevice(int device_id, std::shared_ptr<Driver> driver, int efd, const uint8_t (*mac_addr)[6], std::shared_ptr<GlobalInterrupts> irq_glob) : VmuxDevice(device_id, driver) {
    this->driver = driver;
    memcpy((void*)this->mac_addr, mac_addr, 6);

    for (int idx = 0; idx < NUM_MSIX_IRQs; idx++) {
      auto throttler = std::make_shared<InterruptThrottlerSimbricks>(efd, idx, irq_glob);
      irq_glob->add(throttler);
      this->irqThrottle.push_back(throttler);
    }

    // printf("foobar %zu\n", nicbm::kMaxDmaLen);
    // i40e::i40e_bm* model = new i40e::i40e_bm();
    this->model = std::make_shared<i40e::e810_bm>();

    this->init_pci_ids();
    this->registerDriverEpoll(driver, efd);
    this->rx_callback = E810EmulatedDevice::driver_cb;
  }

  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) {
    this->vfuServer = vfu;

    std::vector<std::shared_ptr<InterruptThrottlerSimbricks>> throttlers;
    for (int idx = 0; idx < NUM_MSIX_IRQs; idx++) {
      auto throttler = this->irqThrottle[idx];
      throttlers.push_back(throttler);
      this->irqThrottle[idx]->vfuServer = vfu;
    }
    this->callbacks = std::make_shared<nicbm::Runner::CallbackAdaptor>(shared_from_this(), &this->mac_addr, this->irqThrottle);
    this->callbacks->model = this->model;
    this->callbacks->vfu = vfu;
    this->model->vmux = this->callbacks;

    // set up vfio-user register mediation
    this->init_bar_callbacks(*vfu);

    // set up irqs
    this->init_irqs(*vfu);

    // init pci capas
    // capa copied from physical E810
    unsigned char msix_capa[] = { 0x11, 0xa0, 0xff, 0x07, 0x03, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00 };
    int ret = vfu_pci_add_capability(vfu->vfu_ctx, 0, 0, msix_capa);
    if (ret < 0)
      die("add cap error");

    // set up libvfio-user callbacks
    // vfu.setup_passthrough_callbacks(this->vfioc);
    this->init_general_callbacks(*vfu);
  };

  void processAllPollTimers() {
    for (size_t i = 0; i < NUM_MSIX_IRQs; i++) {
      this->irqThrottle[i]->processPollTimer();
    }
  }

  // forward rx event callback from tap to this E1000EmulatedDevice
  static void driver_cb(int vm_number, void *this__) {
    E810EmulatedDevice *this_ = (E810EmulatedDevice*) this__;
    this_->processAllPollTimers();
    this_->driver->recv(vm_number); // recv assumes the Device does not handle packet of other VMs until recv_consumed()!
    for (uint16_t i = 0; i < this_->driver->nb_bufs_used; i++) {
      this_->vfu_ctx_mutex.lock();
      this_->model->EthRx(0, this_->driver->rxBuf_queue[i], this_->driver->rxBufs[i], this_->driver->rxBuf_used[i]); // hardcode port 0
      this_->vfu_ctx_mutex.unlock();
      this_->processAllPollTimers();
    }
    this_->driver->recv_consumed(vm_number);
  }

  void init_pci_ids() {
    this->model->SetupIntro(this->deviceIntro);
    this->info.pci_vendor_id = this->deviceIntro.pci_vendor_id;
    this->info.pci_device_id = this->deviceIntro.pci_device_id;
    // Some values are not set by SetupIntro
    this->info.pci_subsystem_vendor_id = 0x0086;
    this->info.pci_subsystem_id = 0x0001;
    this->info.pci_device_revision_id = 0x2;
    this->info.pci_class = this->deviceIntro.pci_class;
    this->info.pci_subclass = this->deviceIntro.pci_subclass;
    this->info.pci_revision = this->deviceIntro.pci_revision;
    __builtin_dump_struct(&this->info, &printf);
    __builtin_dump_struct(&this->deviceIntro, &printf);
    this->model->SetupIntro(this->deviceIntro);
  }

struct timespec read_global_time() {
  struct timespec ts;

  if(this->driver->readCurrentTimestamp(&ts)) {
    
    // fallback
    if(!clock_gettime(CLOCK_MONOTONIC, &ts)) {
      printf("Error: Could not get unix timestamp!\n");
      return {0, 0};
    }
  }
  
  printf("Debug: Read Ts: %lu %lu \n", ts.tv_sec, ts.tv_nsec);
  return ts;
}


struct timespec read_rx_timestamp(uint16_t portid) {
  struct timespec ts;

  if(this->driver->readRxTimestamp(portid, &ts)) {
    // fallback
    return read_global_time();
  }

  printf("Debug: Read RX Ts: %lu %lu \n", ts.tv_sec, ts.tv_nsec);
  return ts;
}

struct timespec read_tx_timestamp(uint16_t portid) {
  struct timespec ts;

  if(this->driver->readTxTimestamp(portid, &ts)) {
    
    // fallback
    return read_global_time();
  }
  
  printf("Debug: Read TX Ts: %lu %lu \n", ts.tv_sec, ts.tv_nsec);
  return ts;
}

private:
  void init_general_callbacks(VfioUserServer &vfu) {
    int ret;
    // I think quiescing only applies when using vfu_add_to_sgl and
    // vfu_sgl_read (see libvfio-user/docs/memory-mapping.md
    // vfu_setup_device_quiesce_cb(this->vfu_ctx,
    //      VfioUserServer::quiesce_cb);
    ret = vfu_setup_device_reset_cb(vfu.vfu_ctx,
                                    E810EmulatedDevice::reset_device_cb);
    if (ret)
      die("setting up reset callback for libvfio-user failed %d", ret);

    ret = vfu_setup_device_dma(vfu.vfu_ctx, E810EmulatedDevice::dma_register_cb,
                               E810EmulatedDevice::dma_unregister_cb);
    if (ret)
      die("setting up dma callback for libvfio-user failed %d", ret);

    ret = vfu_setup_irq_state_callback(
        vfu.vfu_ctx, VFU_DEV_INTX_IRQ,
        E810EmulatedDevice::irq_state_cb);
    if (ret)
      die("setting up intx state callback for libvfio-user failed");

    ret = vfu_setup_irq_state_callback(
        vfu.vfu_ctx, VFU_DEV_MSIX_IRQ,
        E810EmulatedDevice::irq_state_cb);
    if (ret)
      die("setting up msix state callback for libvfio-user failed");

    // register unimplemented callback for all unused interrupt types
    for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
      if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
        continue;
      ret = vfu_setup_irq_state_callback(
          vfu.vfu_ctx, (enum vfu_dev_irq_type)type,
          E810EmulatedDevice::irq_state_cb);
      if (ret)
        die("setting up irq type %d callback for libvfio-user \
                      failed",
            type);
    }
  }

  void init_irqs(VfioUserServer &vfu) {
    int ret = vfu_setup_device_nr_irqs(
      vfu.vfu_ctx, VFU_DEV_MSIX_IRQ, NUM_MSIX_IRQs);
    if (ret < 0) {
      die("Cannot set up vfio-user irq (type %d, num %d)", VFU_DEV_MSIX_IRQ,
          1);
    }
  }

  static int reset_device_cb(vfu_ctx_t *vfu_ctx,
                             [[maybe_unused]] vfu_reset_type_t type) {
    E810EmulatedDevice *device = (E810EmulatedDevice *)vfu_get_private(vfu_ctx);
    printf("resetting device\n"); // this happens at VM boot
    // device->model->SignalInterrupt(1, 1); // just as an example: do stuff
    return 0;
  }

  static void dma_register_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                              [[maybe_unused]] vfu_dma_info_t *info) {
    printf("dma register cb\n");
    std::shared_ptr<VfioUserServer> vfu_ =
        ((E810EmulatedDevice *)vfu_get_private(vfu_ctx))->vfuServer;
    VfioUserServer *vfu =
        vfu_.get(); // lets hope vfu_ stays around until end of this function
                    // and map_dma_here only borrows vfu
    uint32_t flags = 0; // unused here

    VfioUserServer::map_dma_here(vfu_ctx, vfu, info, &flags);
  }
  static void dma_unregister_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                                [[maybe_unused]] vfu_dma_info_t *info) {
    printf("dma unregister cb\n");
    std::shared_ptr<VfioUserServer> vfu_ =
        ((E810EmulatedDevice *)vfu_get_private(vfu_ctx))->vfuServer;
    VfioUserServer *vfu =
        vfu_.get(); // lets hope vfu_ stays around until end of this function
                    // and map_dma_here only borrows vfu
    VfioUserServer::unmap_dma_here(vfu_ctx, vfu, info);
  }

  static void irq_state_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                                         [[maybe_unused]] uint32_t start,
                                         [[maybe_unused]] uint32_t count,
                                         [[maybe_unused]] bool mask) {
    E810EmulatedDevice *this_= (E810EmulatedDevice *)vfu_get_private(vfu_ctx);
    for (uint32_t i = start; i < start + count; i++) {
      this_->irqThrottle[i]->guest_unmasked_irq = !mask;
    }
    if_log_level(LOG_DEBUG,
      printf("irq_state_callback: [%d, %d) masked %d\n", start, start + count, mask));
  }

  void init_bar_callbacks(VfioUserServer &vfu) {
    for (int idx = 0; idx < SIMBRICKS_PROTO_PCIE_NBARS; idx++) {
      auto region = this->deviceIntro.bars[idx];

      int ret;

      if (region.len == 0) {
        printf("Bar region %d skipped.\n", idx);
      }

      // set up register accesses VM<->vmux

      int flags = Util::convert_flags(region.flags);
      flags |= VFU_REGION_FLAG_RW;
      if (idx == E810EmulatedDevice::BAR_REGS) { // the bm only serves registers on bar 2
        ret = vfu_setup_region(vfu.vfu_ctx, idx, region.len,
                               &(this->expected_access_callback), flags, NULL,
                               0,      // nr. items in bar_mmap_areas
                               -1, 0); // fd -1 and offset 0 because fd is unused
      } else {
        ret = vfu_setup_region(vfu.vfu_ctx, idx, region.len,
                               &(this->unexpected_access_callback), flags, NULL,
                               0,      // nr. items in bar_mmap_areas
                               -1, 0); // fd -1 and offset 0 because fd is unused
      }
      if (ret < 0) {
        die("failed to setup BAR region %d", idx);
      }

      // init some flags that are also set with qemu passthrough
      vfu_pci_config_space_t *config_space =
          vfu_pci_get_config_space(vfu.vfu_ctx);
      vfu_bar_t *bar_config = &(config_space->hdr.bars[idx]);
      // see pci spec sec 7.5.1.2.1 for meaning of bits:
      if (region.flags & SIMBRICKS_PROTO_PCIE_BAR_PF) {
        bar_config->mem.prefetchable = 1; // prefetchable
      }
      if (region.flags & SIMBRICKS_PROTO_PCIE_BAR_64) {
        bar_config->mem.locatable = 0b10; // 64 bit
      }

      printf("Vfio-user: Bar region %d \
                (size 0x%x) set up.\n",
             idx, (uint)region.len);
    }
  }

  static ssize_t unexpected_access_callback(
      [[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] char *const buf,
      [[maybe_unused]] size_t count, [[maybe_unused]] __loff_t offset,
      [[maybe_unused]] const bool is_write) {
    printf("WARN: unexpected vfio register/DMA access callback was triggered (at 0x%lx, is write %d).\n",
           offset, is_write);
    return 0;
  }

  static ssize_t expected_access_callback(
      [[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] char *const buf,
      [[maybe_unused]] size_t count, [[maybe_unused]] __loff_t offset,
      [[maybe_unused]] const bool is_write) {
    if_log_level(LOG_DEBUG, 
        printf("a vfio register/DMA access callback was triggered (at 0x%lx, is write %d).\n",
           offset, is_write);
        );
    E810EmulatedDevice *device =
        (E810EmulatedDevice *)vfu_get_private(vfu_ctx);
    if (is_write) {
      device->model->RegWrite(E810EmulatedDevice::BAR_REGS, offset, buf, count);
      return count;
    } else {
      device->model->RegRead(E810EmulatedDevice::BAR_REGS, offset, buf, count);
      return count;
    }
    return 0;
  }
};
