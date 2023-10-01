#pragma once

#include "sims/nic/e810_bm/e810_bm.h"
#include "sims/nic/e1000_bm/i8254xGBe.h"
#include "libsimbricks/simbricks/nicbm/nicbm.h"
#include "util.hpp"
#include "vfio-consumer.hpp"
#include "vfio-server.hpp"
#include <cstdint>
#include <memory>
#include <string>

// /** Number of PCI bars */
// #define SIMBRICKS_PROTO_PCIE_NBARS 6

struct DeviceInfo {
  // /** information for each BAR exposed by the device */
  // struct {
  //   /** length of the bar in bytes (len = 0 indicates unused bar) */
  //   uint64_t len;
  //   /** flags (see SIMBRICKS_PROTO_PCIE_BAR_*) */
  //   uint64_t flags;
  // } __attribute__((packed)) bars[SIMBRICKS_PROTO_PCIE_NBARS];

  // Vendor ID, Device ID, Subsystem Vendor ID, and Subsystem ID
  /** PCI vendor id */
  uint16_t pci_vendor_id;
  /** PCI device id */
  uint16_t pci_device_id;
  /** PCI subsystem vendor id **/
  uint16_t pci_subsystem_vendor_id;
  /** PCI subsystem id **/
  uint16_t pci_subsystem_id;
  /** PCI device revision id **/
  uint8_t pci_device_revision_id;
  /* PCI class */
  uint8_t pci_class;
  /* PCI subclass */
  uint8_t pci_subclass;
  /* PCI revision */
  uint8_t pci_revision;
  // /* PCI prog if */
  // uint8_t pci_progif;
  //
  // /* PCI number of MSI vectors */
  // uint8_t pci_msi_nvecs;
  //
  // /* PCI number of MSI-X vectors */
  // uint16_t pci_msix_nvecs;
  // /* BAR number for MSI-X table */
  // uint8_t pci_msix_table_bar;
  // /* BAR number for MSI-X PBA */
  // uint8_t pci_msix_pba_bar;
  // /* Offset for MSI-X table */
  // uint32_t pci_msix_table_offset;
  // /* Offset for MSI-X PBA */
  // uint32_t pci_msix_pba_offset;
  // /* MSI-X capability offset */
  // uint16_t psi_msix_cap_offset;
};

class VmuxDevice {
  public:
    DeviceInfo info;

    /* vfio endpoint, may be null for some devices */
    std::shared_ptr<VfioConsumer> vfioc;

    /* the implementor is not allowed to keep this vfu, it is only borrowed */
    virtual void setup_vfu(VfioUserServer &vfu) = 0;

    virtual ~VmuxDevice() = default;
};

class StubDevice : public VmuxDevice {
  public:
    StubDevice() {
      this->vfioc = NULL;
    }
    void setup_vfu(VfioUserServer &vfu) {};
};

class E1000EmulatedDevice : public VmuxDevice {
  private:
    std::unique_ptr<IGbE> model;
    /** we don't pass the entire device to the model, but only the callback
     * adaptor. We choose this approach, because we don't want to purpose
     * build the device to fit the simbricks code, and don't want to change
     * the simbricks code too much to fit the vmux code. **/
    std::shared_ptr<nicbm::CallbackAdaptor> callbacks;

    SimbricksProtoPcieDevIntro deviceIntro = SimbricksProtoPcieDevIntro();

  public:
    E1000EmulatedDevice() {

      const auto params = IGbEParams {
        .rx_fifo_size = 384 * 1024,
        .tx_fifo_size = 384 * 1024,
        .fetch_delay = 10 * 1000,
        .wb_delay = 10 * 1000,
        .fetch_comp_delay = 10 * 1000,
        .wb_comp_delay = 10 * 1000,
        .rx_write_delay = 0,
        .tx_read_delay = 0,
        .pio_delay = 0,
        .phy_pid = 0x02A8,
        .phy_epid = 0x0380,
        .rx_desc_cache_size = 64,
        .tx_desc_cache_size = 64,
      };

      this->model = std::make_unique<IGbE>(&params);

      this->callbacks = std::make_shared<nicbm::CallbackAdaptor>();

      this->model->vmux = this->callbacks;
      this->init_pci_ids();
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
      this->model->SetupIntro(this->deviceIntro);
      this->info.pci_vendor_id = this->deviceIntro.pci_vendor_id;
      this->info.pci_device_id = this->deviceIntro.pci_device_id;
      // Some values are not set by SetupIntro
      this->info.pci_subsystem_vendor_id = 0x0086; // TODO(HE7086): find values for E1000
      this->info.pci_subsystem_id = 0x0001;
      this->info.pci_device_revision_id = 0x2;
      this->info.pci_class = this->deviceIntro.pci_class;
      this->info.pci_subclass = this->deviceIntro.pci_subclass;
      this->info.pci_revision = this->deviceIntro.pci_revision;
      /* __builtin_dump_struct(&this->info, &printf); */
      /* __builtin_dump_struct(&this->deviceIntro, &printf); */
      this->model->SetupIntro(this->deviceIntro); // TODO(HE7086): why twice?
    }

  private:
    void init_general_callbacks(VfioUserServer &vfu) {
      // TODO all those callback functions need implementation
      int ret;
      ret = vfu_setup_device_reset_cb(vfu.vfu_ctx,
              E1000EmulatedDevice::reset_device_cb);
      if (ret) die("setting up reset callback for libvfio-user failed %d", ret);

      ret = vfu_setup_device_dma(vfu.vfu_ctx,
              E1000EmulatedDevice::dma_register_cb,
              E1000EmulatedDevice::dma_unregister_cb);
      if (ret) die("setting up dma callback for libvfio-user failed %d", ret);

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx,
              VFU_DEV_INTX_IRQ,
              E1000EmulatedDevice::irq_state_unimplemented_cb);
      if (ret) die("setting up intx state callback for libvfio-user failed");

      ret = vfu_setup_irq_state_callback(vfu.vfu_ctx,
              VFU_DEV_MSIX_IRQ,
              E1000EmulatedDevice::irq_state_unimplemented_cb);
      if (ret) die("setting up msix state callback for libvfio-user failed");

      // register unimplemented callback for all unused interrupt types
      for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
        if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
          continue;
        ret = vfu_setup_irq_state_callback(vfu.vfu_ctx,
                  (enum vfu_dev_irq_type) type,
                  E1000EmulatedDevice::irq_state_unimplemented_cb);
        if (ret) die("setting up irq type %d callback for libvfio-user failed", type);
      }
    }
    static int reset_device_cb(vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_reset_type_t type)
    {
      E1000EmulatedDevice *device = (E1000EmulatedDevice*) vfu_get_private(vfu_ctx);
      printf("resetting device\n");
      return 0;
    }
    static void dma_register_cb(
            [[maybe_unused]] vfu_ctx_t *vfu_ctx,
            [[maybe_unused]] vfu_dma_info_t *info)
    {
      printf("dma register cb\n");
    }
    static void dma_unregister_cb(
            [[maybe_unused]] vfu_ctx_t *vfu_ctx,
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
        int flags = convert_flags(region.flags);
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
        vfu_pci_config_space_t *config_space = vfu_pci_get_config_space(vfu.vfu_ctx);
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
        
        int flags = convert_flags(region.flags);
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

class PassthroughDevice : public VmuxDevice {
  public:
    PassthroughDevice(std::shared_ptr<VfioConsumer> vfioc, std::string pci_address) {
      this->vfioc = vfioc;
      this->init_pci_ids(pci_address);
    }

    void setup_vfu(VfioUserServer &vfu) {
      int ret; 

      // set up vfio-user register passthrough
      if (this->vfioc != NULL) {
          // pass through registers, only if it is a passthrough device
          ret = vfu.add_regions(this->vfioc->regions, this->vfioc->device);
          if (ret < 0)
              die("failed to add regions");
      }

      // set up irqs 
      if (this->vfioc != NULL) {
          ret = vfu.add_irqs(this->vfioc->interrupts);
          if (ret < 0)
              die("failed to add irqs");

          vfu.add_legacy_irq_pollfds(this->vfioc->irqfd_intx, this->vfioc->irqfd_msi,
                  this->vfioc->irqfd_err, this->vfioc->irqfd_req);
          vfu.add_msix_pollfds(this->vfioc->irqfds);
      }

      // set up callbacks
      vfu.setup_passthrough_callbacks(this->vfioc);
    }

  private:
    void init_pci_ids(std::string device) {
      std::string group_arg = get_iommu_group(device);

      //Get Hardware Information from Device
      std::vector<int> pci_ids = get_hardware_ids(device,group_arg);
      if(pci_ids.size() != 5){
          die("Failed to parse Hardware Information, expected %d IDs got %zu\n",
                  5, pci_ids.size());
          // stop_runner(-1,
          // "Failed to parse Hardware Information, expected %d IDs got %zu\n",
          // 5, pci_ids.size());
      }
      this->info.pci_device_revision_id = pci_ids[0];
      pci_ids.erase(pci_ids.begin()); // Only contains Vendor ID, Device ID,
                                      // Subsystem Vendor ID, Subsystem ID now
      this->info.pci_vendor_id = pci_ids[0];
      this->info.pci_device_id = pci_ids[1];
      this->info.pci_subsystem_vendor_id = pci_ids[2];
      this->info.pci_subsystem_id = pci_ids[3];

      // sane defaults for pci (non-device) ids
      this->info.pci_class = 0x2;
      this->info.pci_subclass = 0x0;
      this->info.pci_revision = 0x0;
      __builtin_dump_struct(&this->info, &printf);

      printf("PCI-Device: %s\nIOMMU-Group: %s\nRevision: 0x%02X\n\
              IDs: 0x%04X,0x%04X,0x%04X,0x%04X\n",
              device.c_str(),
              group_arg.c_str(),
              this->info.pci_device_revision_id,
              this->info.pci_vendor_id,
              this->info.pci_device_id,
              this->info.pci_subsystem_vendor_id,
              this->info.pci_subsystem_id);

    }
};
