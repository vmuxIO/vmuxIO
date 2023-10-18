#include "device.hpp"
#include "nic-emu.hpp"

static bool rust_logs_initialized = false;

class E1000EmulatedDevice : public VmuxDevice {
  private:
    E1000FFI* e1000;
    static const int bars_nr = 2;

  public:
    E1000EmulatedDevice() {
      if (!rust_logs_initialized) {
          initialize_rust_logging(4); // Debug logs
          rust_logs_initialized = true;
      }

      auto callbacks = FfiCallbacks {
          this,
          send_cb,
          dma_read_cb,
          dma_write_cb,
          issue_interrupt_cb,
      };

      e1000 = new_e1000(callbacks);

      this->init_pci_ids();
    }

    ~E1000EmulatedDevice() {
        drop_e1000(e1000);
    }

    void setup_vfu(VfioUserServer &vfu) {
      // set up vfio-user register mediation
      this->init_bar_callbacks(vfu);

      // set up irqs 
      // TODO

      // set up libvfio-user callbacks
      // vfu.setup_passthrough_callbacks(this->vfioc);
      this->init_general_callbacks(vfu);
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
        die("Send CB\n"); // TODO connect this and the ones below
    }

    static void dma_read_cb(void *private_ptr, uintptr_t dma_address, uint8_t *buffer, uintptr_t len) {
        die("Dma read CB\n");
    }

    static void dma_write_cb(void *private_ptr, uintptr_t dma_address, const uint8_t *buffer, uintptr_t len) {
        die("Dma write CB\n");
    }

    static void issue_interrupt_cb(void *private_ptr) {
        die("Issue interrupt CB\n");
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
        die("setting up reset callback for libvfio-user failed %d",
                  ret);

      ret = vfu_setup_device_dma(vfu.vfu_ctx,
              E1000EmulatedDevice::dma_register_cb,
              E1000EmulatedDevice::dma_unregister_cb);
      if (ret)
        die("setting up dma callback for libvfio-user failed %d",
                  ret);

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx, VFU_DEV_INTX_IRQ,
              E1000EmulatedDevice::irq_state_unimplemented_cb);
      if (ret)
        die("setting up intx state callback for libvfio-user failed");

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx, VFU_DEV_MSIX_IRQ,
              E1000EmulatedDevice::irq_state_unimplemented_cb);
      if (ret)
        die("setting up msix state callback for libvfio-user failed");

      // register unimplemented callback for all unused interrupt types
      for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
        if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
          continue;
        ret = vfu_setup_irq_state_callback(vfu.vfu_ctx,
                  (enum vfu_dev_irq_type) type,
                  E1000EmulatedDevice::irq_state_unimplemented_cb);
        if (ret)
          die("setting up irq type %d callback for libvfio-user \
                      failed", type);
      }
    }
    static int reset_device_cb(vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_reset_type_t type)
    {
      E1000EmulatedDevice *device = (E1000EmulatedDevice*) vfu_get_private(vfu_ctx);
      printf("resetting device\n"); // this happens at VM boot
      // device->model->SignalInterrupt(1, 1); // just as an example: do stuff
      return 0;
    }
    static void dma_register_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_dma_info_t *info)
    {
      printf("dma register cb\n");
    }
    static void dma_unregister_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_dma_info_t *info)
    {
      printf("dma unregister cb\n");
    }
    static void irq_state_unimplemented_cb(
            [[maybe_unused]] vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] uint32_t start,
            [[maybe_unused]] uint32_t count,
            [[maybe_unused]] bool mask
            )
    {
        printf("irq_state_unimplemented_cb unimplemented\n");
    }

    void init_bar_callbacks(VfioUserServer &vfu) {
      int ret;

      // set up register accesses VM<->vmux
      int bar_idx = 0;
      int bar_len = 0x20000;
      
      int flags = VFU_REGION_FLAG_MEM;
      flags |= VFU_REGION_FLAG_RW;
      ret = vfu_setup_region(vfu.vfu_ctx, bar_idx,
              bar_len,
              &(this->expected_access_callback),
              flags, NULL, 
              0, // nr. items in bar_mmap_areas
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
              (size 0x%x) set up.\n", bar_idx,
              (uint)bar_len);
    }
    static ssize_t expected_access_callback(
              [[maybe_unused]] vfu_ctx_t *vfu_ctx,
              [[maybe_unused]] char * const buf,
              [[maybe_unused]] size_t count,
              [[maybe_unused]] __loff_t offset,
              [[maybe_unused]] const bool is_write
              )
      {
        E1000EmulatedDevice *device = (E1000EmulatedDevice*) vfu_get_private(vfu_ctx);
        printf("a vfio register/DMA access callback was triggered (at 0x%lx, is write %d).\n",
                offset, is_write);
        if (e1000_region_access(device->e1000, 0, offset, (uint8_t*) buf, count, is_write)) { // TODO fixed bar number
          return count;
        }
        return 0;
      }
};

