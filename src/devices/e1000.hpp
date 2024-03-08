#pragma once

#include "devices/vmux-device.hpp"
#include "nic-emu.hpp"
#include "src/drivers/tap.hpp"
#include "util.hpp"
#include <bits/types/struct_itimerspec.h>
#include <cstring>
#include <ctime>
#include <memory>
#include <sys/timerfd.h>
#include <time.h>
#include <cstdlib>
#include "interrupts/global.hpp"
#include "vfio-server.hpp"
#include "interrupts/accurate.hpp"
#include "interrupts/qemu.hpp"
#include "interrupts/none.hpp"

static bool rust_logs_initialized = false;


#define IRQ_IDX 0 // this emulator may register multiple interrupts, but only uses the first one

class E1000EmulatedDevice : public VmuxDevice {
private:
  E1000FFI *e1000;
  std::shared_ptr<InterruptThrottlerNone> irqThrottle;
  static const int bars_nr = 2;
  epoll_callback tapCallback;
  int efd = 0; // if non-null: eventfd registered for this->tap->fd

  void registerDriverEpoll(std::shared_ptr<Driver> driver, int efd) {
    if (driver->fd == 0)
      return;
      // die("E1000 only supports drivers that offer fds to wait on")

    this->tapCallback.fd = driver->fd;
    this->tapCallback.callback = E1000EmulatedDevice::driver_cb;
    this->tapCallback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->tapCallback;

    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, driver->fd, &e))
      die("could not register driver fd to epoll");

    this->efd = efd;
  }

public:
  E1000EmulatedDevice(std::shared_ptr<Driver> driver, int efd, bool spaced_interrupts, std::shared_ptr<GlobalInterrupts> globalIrq, const uint8_t (*mac_addr)[6]) : VmuxDevice(driver) {
    this->irqThrottle = std::make_shared<InterruptThrottlerNone>(efd, IRQ_IDX, globalIrq);
    globalIrq->add(this->irqThrottle);
    if (!rust_logs_initialized) {
      if (LOG_LEVEL <= LOG_ERR) {
        initialize_rust_logging(0);
      } else {
        initialize_rust_logging(6); // Debug logs
      }
      rust_logs_initialized = true;
    }

    auto irq_cb = issue_interrupt_cb;
    if (spaced_interrupts)
      irq_cb = issue_spaced_interrupt_cb;

    auto callbacks = FfiCallbacks{
        this, send_cb, dma_read_cb, dma_write_cb, irq_cb,
    };

    e1000 = new_e1000(callbacks, mac_addr);

    this->init_pci_ids();

    this->registerDriverEpoll(driver, efd);
    this->rx_callback = E1000EmulatedDevice::driver_cb;
  }

  ~E1000EmulatedDevice() { 
    drop_e1000(e1000); 
    epoll_ctl(this->efd, EPOLL_CTL_DEL, this->efd, (struct epoll_event *)NULL); // does nothing of efd is not initialized (== 0)
  }

  // forward rx event callback from tap to this E1000EmulatedDevice
  static void driver_cb(int vm_number, void *this__) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice*) this__;
    if (e1000_rx_is_ready(this_->e1000)) {
      this_->driver->recv(vm_number); // recv assumes the Device does not handle packet of other VMs until recv_consumed()!
      for (uint16_t i = 0; i < this_->driver->nb_bufs_used; i++) {
        while(!e1000_rx_is_ready(this_->e1000)) {
          // blocking pause to reduce memory contention while spinning.
          // 100us seem to yield good results.
          Util::rte_delay_us_block(100);
        }
        this_->vfu_ctx_mutex.lock();
        this_->ethRx(this_->driver->rxBufs[i], this_->driver->rxBuf_used[i]);
        this_->vfu_ctx_mutex.unlock();
      }
      this_->driver->recv_consumed(vm_number);
      // printf("interrupt_throtteling register: %d\n", e1000_interrupt_throtteling_reg(this_->e1000, -1));
    }
  }

  void ethRx(char *data, size_t len) {
    if (e1000_rx_is_ready(e1000)) {
      e1000_receive(e1000, (uint8_t*)data, len);
    } else {
      printf("E1000EmulatedDevice: Dropping received packet because e1000 is not ready\n");
    }
  }

  void setup_vfu(std::shared_ptr<VfioUserServer> vfu) override {
    this->vfuServer = vfu;
    this->irqThrottle->vfuServer = vfu;

    // set up vfio-user register mediation
    this->init_bar_callbacks(*vfu);

    // set up irqs
    this->init_irqs(*vfu);

    // set up libvfio-user callbacks
    this->init_general_callbacks(*vfu);

    int ret =
        vfu_setup_device_dma(vfu->vfu_ctx, E1000EmulatedDevice::dma_register_cb,
                             E1000EmulatedDevice::dma_unregister_cb);

    if (ret)
      die("setting up dma callback for libvfio-user failed %d", ret);
  };

  void init_pci_ids() {
    this->info.pci_vendor_id = 0x8086;
    this->info.pci_device_id = 0x100e;
    // Some values are not set by SetupIntro
    this->info.pci_subsystem_vendor_id = 0x0086;
    this->info.pci_subsystem_id = 0x0001;
    this->info.pci_device_revision_id = 0x2;
    this->info.pci_class = 0x02;
    this->info.pci_subclass = 0x00;
    this->info.pci_revision = 0x3;
    __builtin_dump_struct(&this->info, &printf);
  }

private:
  static void send_cb(void *private_ptr, const uint8_t *buffer, uintptr_t len) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    if_log_level(LOG_DEBUG, {
      printf("received packet for tx:\n");
      Util::dump_pkt((void *)buffer, (size_t)len);
    });
    this_->driver->send((char*)buffer, len);
  }

  static void dma_read_cb(void *private_ptr, uintptr_t dma_address,
                          uint8_t *buffer, uintptr_t len) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    void *local_addr = this_->vfuServer->dma_local_addr(dma_address, len);
    if (!local_addr) {
      die("Could not translate DMA address");
    }
    memcpy(buffer, local_addr, len);
  }

  static void dma_write_cb(void *private_ptr, uintptr_t dma_address,
                           const uint8_t *buffer, uintptr_t len) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    void *local_addr = this_->vfuServer->dma_local_addr(dma_address, len);
    if (!local_addr) {
      die("Could not translate DMA address");
    }
    memcpy(local_addr, buffer, len);
  }

  static void issue_interrupt_cb(void *private_ptr, bool int_pending) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    int ret = vfu_irq_trigger(this_->vfuServer->vfu_ctx, IRQ_IDX);
    if_log_level(LOG_DEBUG, printf("Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
  }

  static void issue_spaced_interrupt_cb(void *private_ptr, bool int_pending) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    // spacing_s = 1 / ( 10^9 / (reg * 256) )
    // spcaing_s = (reg * 256) / 10^9
    ulong spacing_us = (e1000_interrupt_throtteling_reg(this_->e1000, -1) * 256);
    this_->irqThrottle->try_interrupt(spacing_us, int_pending);
    // die("Issue interrupt CB\n");
  }

  void init_general_callbacks(VfioUserServer &vfu) {
    // TODO all those callback functions need implementation
    int ret;
    // I think quiescing only applies when using vfu_add_to_sgl and
    // vfu_sgl_read (see libvfio-user/docs/memory-mapping.md
    // vfu_setup_device_quiesce_cb(this->vfu_ctx,
    //      VfioUserServer::quiesce_cb);
    ret = vfu_setup_device_reset_cb(vfu.vfu_ctx,
                                    E1000EmulatedDevice::reset_device_cb);
    if (ret)
      die("setting up reset callback for libvfio-user failed %d", ret);

    ret =
        vfu_setup_device_dma(vfu.vfu_ctx, E1000EmulatedDevice::dma_register_cb,
                             E1000EmulatedDevice::dma_unregister_cb);
    if (ret)
      die("setting up dma callback for libvfio-user failed %d", ret);

    ret = vfu_setup_irq_state_callback(
        vfu.vfu_ctx, VFU_DEV_INTX_IRQ,
        E1000EmulatedDevice::irq_state_unimplemented_cb);
    if (ret)
      die("setting up intx state callback for libvfio-user failed");

    ret = vfu_setup_irq_state_callback(
        vfu.vfu_ctx, VFU_DEV_MSIX_IRQ,
        E1000EmulatedDevice::irq_state_unimplemented_cb);
    if (ret)
      die("setting up msix state callback for libvfio-user failed");

    // register unimplemented callback for all unused interrupt types
    for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
      if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
        continue;
      ret = vfu_setup_irq_state_callback(
          vfu.vfu_ctx, (enum vfu_dev_irq_type)type,
          E1000EmulatedDevice::irq_state_unimplemented_cb);
      if (ret)
        die("setting up irq type %d callback for libvfio-user \
                      failed",
            type);
    }
  }
  static int reset_device_cb(vfu_ctx_t *vfu_ctx,
                             [[maybe_unused]] vfu_reset_type_t type) {
    E1000EmulatedDevice *device =
        (E1000EmulatedDevice *)vfu_get_private(vfu_ctx);
    printf("resetting device\n"); // this happens at VM boot
    // device->model->SignalInterrupt(1, 1); // just as an example: do stuff
    return 0;
  }

  static void dma_register_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                              [[maybe_unused]] vfu_dma_info_t *info) {
    printf("dma register cb\n");
    std::shared_ptr<VfioUserServer> vfu_ =
        ((E1000EmulatedDevice *)vfu_get_private(vfu_ctx))->vfuServer;
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
        ((E1000EmulatedDevice *)vfu_get_private(vfu_ctx))->vfuServer;
    VfioUserServer *vfu =
        vfu_.get(); // lets hope vfu_ stays around until end of this function
                    // and map_dma_here only borrows vfu
    VfioUserServer::unmap_dma_here(vfu_ctx, vfu, info);
  }
  static void irq_state_unimplemented_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                                         [[maybe_unused]] uint32_t start,
                                         [[maybe_unused]] uint32_t count,
                                         [[maybe_unused]] bool mask) {
    printf("irq_state_unimplemented_cb unimplemented\n");
  }

  void init_irqs(VfioUserServer &vfu) {
    int ret = vfu_setup_device_nr_irqs(
      vfu.vfu_ctx, VFU_DEV_INTX_IRQ, 1);
    if (ret < 0) {
      die("Cannot set up vfio-user irq (type %d, num %d)", VFIO_PCI_INTX_IRQ_INDEX,
          1);
    }

    // TODO needs capability
    // ret = vfu_setup_device_nr_irqs(
    //   vfu.vfu_ctx, VFU_DEV_MSI_IRQ, 1);
    // if (ret < 0) {
    //   die("Cannot set up vfio-user irq (type %d, num %d)", VFIO_PCI_MSI_IRQ_INDEX,
    //       1);
    // }
  }

  void init_bar_callbacks(VfioUserServer &vfu) {
    int ret;

    // set up register accesses VM<->vmux
    int bar_idx = 0;
    int bar_len = 0x20000;

    int flags = VFU_REGION_FLAG_MEM;
    flags |= VFU_REGION_FLAG_RW;
    ret = vfu_setup_region(vfu.vfu_ctx, bar_idx, bar_len, &(this->bar0callback),
                           flags, NULL,
                           0,      // nr. items in bar_mmap_areas
                           -1, 0); // fd -1 and offset 0 because fd is unused
    if (ret < 0) {
      die("failed to setup BAR region %d", bar_idx);
    }

    bar_idx = 1;
    bar_len = 0x40;

    flags = 0; // actually PIO for once
    flags |= VFU_REGION_FLAG_RW;
    ret = vfu_setup_region(vfu.vfu_ctx, bar_idx, bar_len, &(this->bar1callback),
                           flags, NULL,
                           0,      // nr. items in bar_mmap_areas
                           -1, 0); // fd -1 and offset 0 because fd is unused
    if (ret < 0) {
      die("failed to setup BAR region %d", bar_idx);
    }

    // // init some flags that are also set with qemu passthrough
    // vfu_pci_config_space_t *config_space =
    //     vfu_pci_get_config_space(vfu.vfu_ctx);
    // vfu_bar_t *bar_config = &(config_space->hdr.bars[bar_idx]);
    // // see pci spec sec 7.5.1.2.1 for meaning of bits:
    // if (region.flags & SIMBRICKS_PROTO_PCIE_BAR_PF) {
    //   bar_config->mem.prefetchable = 1; // prefetchable
    // }
    // if (region.flags & SIMBRICKS_PROTO_PCIE_BAR_64) {
    //   bar_config->mem.locatable = 0b10; // 64 bit
    // }

    printf("Vfio-user: Bar region %d \
              (size 0x%x) set up.\n",
           bar_idx, (uint)bar_len);
  }
  static ssize_t bar0callback([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                              [[maybe_unused]] char *const buf,
                              [[maybe_unused]] size_t count,
                              [[maybe_unused]] __loff_t offset,
                              [[maybe_unused]] const bool is_write) {
    E1000EmulatedDevice *device =
        (E1000EmulatedDevice *)vfu_get_private(vfu_ctx);
    // printf("a vfio register/DMA access callback was triggered (at 0x%lx, is
    // write %d).\n", offset, is_write);
    if (e1000_region_access(device->e1000, 0, offset, (uint8_t *)buf, count,
                            is_write)) { // TODO fixed bar number
      return count;
    }
    return 0;
  }

  static ssize_t bar1callback([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                              [[maybe_unused]] char *const buf,
                              [[maybe_unused]] size_t count,
                              [[maybe_unused]] __loff_t offset,
                              [[maybe_unused]] const bool is_write) {
    E1000EmulatedDevice *device =
        (E1000EmulatedDevice *)vfu_get_private(vfu_ctx);
    // printf("a vfio register/DMA access callback was triggered (at 0x%lx, is
    // write %d).\n", offset, is_write);
    if (e1000_region_access(device->e1000, 1, offset, (uint8_t *)buf, count,
                            is_write)) { // TODO fixed bar number
      return count;
    }
    return 0;
  }
};
