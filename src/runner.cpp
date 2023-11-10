#include "src/runner.hpp"
#include "src/devices/vmux-device.hpp"
#include "caps.hpp"
#include <memory>


#define stop_runner(s,str, ...) do { \
    state.store(s);                  \
    printf(str,__VA_ARGS__);         \
    return;                          \
} while(0)

using std::shared_ptr;

static void _log([[maybe_unused]] vfu_ctx_t *vfu_ctx,
                 [[maybe_unused]] int level, char const *msg) {
  fprintf(stderr, "server[%d]: %s\n", getpid(), msg);
}

void VmuxRunner::run()
{
    this->initilize();
    state.store(INITILIZED);
    printf("%s: Waiting for qemu to attach...\n",this->socket.c_str());
    while(1){
        int ret = vfu_attach_ctx(vfu->vfu_ctx);
        if (ret < 0) {
            usleep(10000);

            if(!running.load()){
                break;
            }
            continue;
        }
        break;
    }   
    state.store(CONNECTED);

    struct pollfd pfd = (struct pollfd) {
        .fd = vfu_get_poll_fd(vfu->vfu_ctx),
            .events = POLLIN
    };

    while(running.load()) {
        int ret = poll(&pfd, 1, 500);

        if (pfd.revents & POLLIN) {
            ret = vfu_run_ctx(vfu->vfu_ctx);
            if (ret < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                if (errno == ENOTCONN) {
                    die("vfu_run_ctx() does not want to run anymore");
                }
                // perhaps is there also an ESHUTDOWN case?
                die("vfu_run_ctx() failed (to realize device emulation)");
            }
        }
    }


}


void VmuxRunner::initilize(){
    state.store(STARTED);
    running.store(1);

    printf("initialize %s\n", vfu->sock.c_str());

    if (vfu->vfu_ctx == NULL) {
        die("failed to initialize device emulation");
    }

    int ret = vfu_setup_log(vfu->vfu_ctx, _log, LOG_DEBUG);
    if (ret < 0) {
        die("failed to setup log");
    }

    ret = vfu_pci_init(vfu->vfu_ctx, VFU_PCI_TYPE_EXPRESS,
            PCI_HEADER_TYPE_NORMAL, 0); // maybe 4?
    if (ret < 0) {
        die("vfu_pci_init() failed") ;
    }

    vfu_pci_set_id(vfu->vfu_ctx, device->info.pci_vendor_id, device->info.pci_device_id,
            device->info.pci_subsystem_vendor_id, device->info.pci_subsystem_id);
    vfu_pci_config_space_t *config_space =
        vfu_pci_get_config_space(vfu->vfu_ctx);
    config_space->hdr.rid = device->info.pci_revision;
    vfu_pci_set_class(vfu->vfu_ctx, device->info.pci_class, device->info.pci_subclass, device->info.pci_revision);

    this->device->setup_vfu(vfu);

    if (device->vfioc != NULL) {
        if(device->vfioc->is_pcie){
            this->add_caps(device->vfioc);
        }
    }

    ret = vfu_realize_ctx(vfu->vfu_ctx);
    if (ret < 0) {
        die("failed to realize device");
    }
}

void VmuxRunner::add_caps(shared_ptr<VfioConsumer> vfioc) {
  std::shared_ptr<Capabilities> caps = std::make_shared<Capabilities>(
      &(vfioc->regions[VFU_PCI_DEV_CFG_REGION_IDX]), vfioc->device_name);
  void *cap_data;

  cap_data = caps->pm();
  int ret = vfu_pci_add_capability(vfu->vfu_ctx, 0, 0, cap_data);
  if (ret < 0)
    die("add cap error");
  free(cap_data);

  cap_data = caps->msix();
  ret = vfu_pci_add_capability(vfu->vfu_ctx, 0, 0, cap_data);
  if (ret < 0)
    die("add cap error");
  free(cap_data);

  // not supported by libvfio-user:
  // cap_data = caps.msi();
  // ret = vfu_pci_add_capability(vfu.vfu_ctx, 0,
  //   VFU_CAP_FLAG_READONLY, cap_data);
  // if (ret < 0)
  //   die("add cap error");
  // free(cap_data);

  cap_data = caps->exp();
  ret = vfu_pci_add_capability(vfu->vfu_ctx, 0, VFU_CAP_FLAG_READONLY, cap_data);
  if (ret < 0)
    die("add cap error");
  free(cap_data);

  // not supported by upstream libvfio-user, not used by linux ice driver
  // cap_data = caps.vpd();
  // ret = vfu_pci_add_capability(vfu.vfu_ctx, 0,
  //   VFU_CAP_FLAG_READONLY, cap_data);
  // if (ret < 0)
  //   die("add cap error");
  // free(cap_data);

  cap_data = caps->dsn();
  ret = vfu_pci_add_capability(
      vfu->vfu_ctx, 0, VFU_CAP_FLAG_READONLY | VFU_CAP_FLAG_EXTENDED, cap_data);
  if (ret < 0)
    die("add cap error");
  free(cap_data);

  // see lspci about which fields mean what
  // https://github.com/pciutils/pciutils/blob/42e6a803bda392e98276b71994db0b0dd285cab1/ls-caps.c#L1469
  // struct msixcap data;
  // data.hdr = {
  //   .id = PCI_CAP_ID_MSIX,
  // };
  // data.mxc = {
  //   // message control
  // };
  // data.mtab = {
  //   // table offset / Table BIR
  // };
  // data.mpba = {
  //   // PBA offset / PBA BIR
  // };
  // //ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, 0, &data);
  // if (ret < 0) {
  //   die("Vfio-user: cannot add MSIX capability");
  // }
  this->caps = caps;
}
