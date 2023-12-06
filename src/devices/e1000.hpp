#pragma once

#include "devices/vmux-device.hpp"
#include "nic-emu.hpp"
#include "tap.hpp"
#include "util.hpp"
#include <bits/types/struct_itimerspec.h>
#include <cstring>
#include <ctime>
#include <sys/timerfd.h>
#include <time.h>

static bool rust_logs_initialized = false;

class E1000EmulatedDevice;

/*
 * Does many things, but the "physical" limit of e1000 of ~8000irq/s is enforced by behavioral model
 */
class InterruptThrottler {
  public:
  struct timespec last_interrupt_ts;
  ulong iterrupt_spacing = 250 * 1000; // nsec
  std::atomic<bool> is_deferred;
  int timer_fd;
  int efd;
  int irq_idx;
  epoll_callback timer_callback;
  std::shared_ptr<VfioUserServer> vfuServer;

  InterruptThrottler(int efd, int irq_idx): irq_idx(irq_idx) {
    this->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0); // foo error
    this->registerEpoll(efd);

  }

  void registerEpoll(int efd) {
    this->timer_callback.fd = this->timer_fd;
    this->timer_callback.callback = InterruptThrottler::timer_cb;
    this->timer_callback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->timer_callback;
    
    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, this->timer_fd, &e))
      die("could not register timer fd to epoll");

    this->efd = efd;
  }

  static void timer_cb(int fd, void* this__) {
    InterruptThrottler* this_ = (InterruptThrottler*) this__;
    this_->is_deferred.store(false);
    this_->send_interrupt();
    struct itimerspec its = {};
    timerfd_settime(this_->timer_fd, 0, &its, NULL); // foo error
  }
                            
  void defer_interrupt(int duration_us) {
    this->is_deferred.store(true);

    struct itimerspec its = {};
    its.it_value.tv_nsec = 1000 * duration_us;
    // its.it_interval.tv_sec = 99999;
    timerfd_settime(this->timer_fd, 0, &its, NULL); // foo error
  }

  __attribute__((noinline)) void try_interrupt() {
    // struct itimerspec its = {};
    // timerfd_gettime(this->timer_fd, &its); // foo error
    // struct timespec* now = &its.it_value;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    ulong time_since_interrupt = Util::diff_timespec(&now, &this->last_interrupt_ts);
    ulong defer_by = this->iterrupt_spacing - time_since_interrupt;
    this->last_interrupt_ts = now;
    if (time_since_interrupt < this->iterrupt_spacing) {
      if (!this->is_deferred.load()) {
        this->last_interrupt_ts.tv_nsec += defer_by;
        // ignore tv_nsec overflows. I think they will just lead to additional interrupts
        this->defer_interrupt(defer_by);
      }
    } else {
      this->send_interrupt();
    }
  }

  __attribute__((noinline)) void send_interrupt() {
    int ret = vfu_irq_trigger(this->vfuServer->vfu_ctx, this->irq_idx);
    if_log_level(LOG_DEBUG, printf("Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
    if (ret < 0) {
      die("Cannot trigger MSIX interrupt %d", this->irq_idx);
    }
  }
};

class E1000EmulatedDevice : public VmuxDevice {
private:
  E1000FFI *e1000;
  std::shared_ptr<Tap> tap;
  InterruptThrottler irqThrottle;
  static const int bars_nr = 2;
  static const int IRQ_IDX = 0; // this emulator may register multiple interrupts, but only uses the first one
  epoll_callback tapCallback;
  int efd = 0; // if non-null: eventfd registered for this->tap->fd

  void registerTapEpoll(std::shared_ptr<Tap> tap, int efd) {
    this->tapCallback.fd = tap->fd;
    this->tapCallback.callback = E1000EmulatedDevice::tap_cb;
    this->tapCallback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->tapCallback;

    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, tap->fd, &e))
      die("could not register tap fd to epoll");

    this->efd = efd;
  }

public:
  E1000EmulatedDevice(std::shared_ptr<Tap> tap, int efd) : tap(tap), irqThrottle(efd, E1000EmulatedDevice::IRQ_IDX) {
    if (!rust_logs_initialized) {
      if (LOG_LEVEL <= LOG_ERR) {
        initialize_rust_logging(0);
      } else {
        initialize_rust_logging(6); // Debug logs
      }
      rust_logs_initialized = true;
    }

    auto callbacks = FfiCallbacks{
        this, send_cb, dma_read_cb, dma_write_cb, issue_interrupt_cb,
    };

    e1000 = new_e1000(callbacks);

    this->init_pci_ids();

    this->registerTapEpoll(tap, efd);
  }

  ~E1000EmulatedDevice() { 
    drop_e1000(e1000); 
    epoll_ctl(this->efd, EPOLL_CTL_DEL, this->efd, (struct epoll_event *)NULL); // does nothing of efd is not initialized (== 0)
  }

  // forward rx event callback from tap to this E1000EmulatedDevice
  static void tap_cb(int fd, void *this__) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice*) this__;
    if (e1000_rx_is_ready(this_->e1000)) {
      this_->tap->recv();
      this_->ethRx((char*)&(this_->tap->rxFrame), this_->tap->rxFrame_used);
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
    VmuxDevice::setup_vfu(vfu);
    this->irqThrottle.vfuServer = vfu;

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
    this_->tap->send((char*)buffer, len);
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

  static void issue_interrupt_cb(void *private_ptr) {
    E1000EmulatedDevice *this_ = (E1000EmulatedDevice *)private_ptr;
    this_->irqThrottle.try_interrupt();
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
