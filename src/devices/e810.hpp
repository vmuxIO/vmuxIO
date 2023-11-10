#pragma once

#include "sims/nic/e810_bm/e810_bm.h"
#include "libsimbricks/simbricks/nicbm/nicbm.h"
#include "util.hpp"
#include "vfio-consumer.hpp"
#include "vfio-server.hpp"
#include "src/devices/vmux-device.hpp"
#include <cstdint>
#include <memory>
#include <string>

class E810EmulatedDevice : public VmuxDevice {
  private:
    std::unique_ptr<i40e::i40e_bm> model; // TODO rename i40e_bm class to e810
                                          //
    /** we don't pass the entire device to the model, but only the callback
     * adaptor. We choose this approach, because we don't want to purpose
     * build the device to fit the simbricks code, and don't want to change
     * the simbricks code too much to fit the vmux code. **/
    std::shared_ptr<nicbm::CallbackAdaptor> callbacks;

    SimbricksProtoPcieDevIntro deviceIntro = SimbricksProtoPcieDevIntro();

  public:
    E810EmulatedDevice() {
      // printf("foobar %zu\n", nicbm::kMaxDmaLen);
      // i40e::i40e_bm* model = new i40e::i40e_bm();
      this->model = std::make_unique<i40e::i40e_bm>();

      this->callbacks = std::make_shared<nicbm::CallbackAdaptor>();

      this->model->vmux = this->callbacks;
      this->init_pci_ids();
    }

    void setup_vfu(std::shared_ptr<VfioUserServer> vfu) {
      // set up vfio-user register mediation
      this->init_bar_callbacks(*vfu);

      // set up irqs 
      // TODO

      // set up libvfio-user callbacks
      // vfu.setup_passthrough_callbacks(this->vfioc);
      this->init_general_callbacks(*vfu);
    };

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

  private:
    void init_general_callbacks(VfioUserServer &vfu) {
      // TODO all those callback functions need implementation
      int ret;
      // I think quiescing only applies when using vfu_add_to_sgl and
      // vfu_sgl_read (see libvfio-user/docs/memory-mapping.md
      // vfu_setup_device_quiesce_cb(this->vfu_ctx,
      //      VfioUserServer::quiesce_cb);
      ret = vfu_setup_device_reset_cb(vfu.vfu_ctx,
              E810EmulatedDevice::reset_device_cb);
      if (ret)
        die("setting up reset callback for libvfio-user failed %d",
                  ret);

      ret = vfu_setup_device_dma(vfu.vfu_ctx,
              E810EmulatedDevice::dma_register_cb,
              E810EmulatedDevice::dma_unregister_cb);
      if (ret)
        die("setting up dma callback for libvfio-user failed %d",
                  ret);

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx, VFU_DEV_INTX_IRQ,
              E810EmulatedDevice::irq_state_unimplemented_cb);
      if (ret)
        die("setting up intx state callback for libvfio-user failed");

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx, VFU_DEV_MSIX_IRQ,
              E810EmulatedDevice::irq_state_unimplemented_cb);
      if (ret)
        die("setting up msix state callback for libvfio-user failed");

      // register unimplemented callback for all unused interrupt types
      for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
        if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
          continue;
        ret = vfu_setup_irq_state_callback(vfu.vfu_ctx,
                  (enum vfu_dev_irq_type) type,
                  E810EmulatedDevice::irq_state_unimplemented_cb);
        if (ret)
          die("setting up irq type %d callback for libvfio-user \
                      failed", type);
      }
    }
    static int reset_device_cb(vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_reset_type_t type)
    {
      E810EmulatedDevice *device = (E810EmulatedDevice*) vfu_get_private(vfu_ctx);
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
      for (int idx = 0; idx < SIMBRICKS_PROTO_PCIE_NBARS; idx++) {
        auto region = this->deviceIntro.bars[idx];

        int ret;

        if (region.len == 0) {
            printf("Bar region %d skipped.\n", idx);
        }

        // set up register accesses VM<->vmux
        
        int flags = Util::convert_flags(region.flags);
        flags |= VFU_REGION_FLAG_RW;
        ret = vfu_setup_region(vfu.vfu_ctx, idx,
                region.len,
                &(this->expected_access_callback),
                flags, NULL, 
                0, // nr. items in bar_mmap_areas
                -1, 0); // fd -1 and offset 0 because fd is unused
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
                (size 0x%x) set up.\n", idx,
                (uint)region.len);
      }
    }
    static ssize_t expected_access_callback(
              [[maybe_unused]] vfu_ctx_t *vfu_ctx,
              [[maybe_unused]] char * const buf,
              [[maybe_unused]] size_t count,
              [[maybe_unused]] __loff_t offset,
              [[maybe_unused]] const bool is_write
              )
      {
        printf("a vfio register/DMA access callback was \
                triggered (at 0x%lx, is write %d.\n",
                offset, is_write);
        return 0;
      }
};
