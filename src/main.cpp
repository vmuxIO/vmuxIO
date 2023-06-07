#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include <string>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <exception>
#include <stdexcept>
#include <optional>
#include <dirent.h>
#include <set>

#include "src/vfio-consumer.hpp"
#include "src/util.hpp"
#include "src/caps.hpp"

extern "C" {
  #include "libvfio-user.h"
}


#include <signal.h>
#include <atomic>

// set true by signals, should be respected by runtime loops
std::atomic<bool> quit(false); 

typedef struct {
  uint64_t value[2];
  void *bar1;
  size_t bar1size;
} vmux_dev_ctx_t;

static void
_log([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] int level, char const *msg)
{
    fprintf(stderr, "server[%d]: %s\n", getpid(), msg);
}

// keep as reference for now, how bar callback functions should work
[[maybe_unused]] static ssize_t
bar0_access(vfu_ctx_t *vfu_ctx, char * const buf, size_t count, __loff_t offset,
            const bool is_write)
{
  vmux_dev_ctx_t *dev_ctx = (vmux_dev_ctx_t*)vfu_get_private(vfu_ctx);

  if (count > sizeof(dev_ctx->value) || offset + count > sizeof(dev_ctx->value)) {
    vfu_log(vfu_ctx, LOG_ERR, "bad BAR0 access %#llx-%#llx",
            (unsigned long long)offset,
            (unsigned long long)offset + count - 1);
    errno = EINVAL;
    return -1;
  }

  vfu_log(vfu_ctx, LOG_ERR, "BAR0 access :)");
  if (is_write) {
    memcpy((&dev_ctx->value) + offset, buf, count);
  } else {
    memcpy(buf, (&dev_ctx->value) + offset, count);
  }

  return count;
}

class VfioUserServer {
  public:
    vfu_ctx_t *vfu_ctx;
    std::string sock;
    std::optional<size_t> run_ctx_pollfd_idx; // index of pollfd in pollfds
    size_t irq_intx_pollfd_idx; // only one
    size_t irq_msi_pollfd_idx; // only one
    size_t irq_msix_pollfd_idx; // multiple
    size_t irq_msix_pollfd_count;
    size_t irq_err_pollfd_idx; // only one
    size_t irq_req_pollfd_idx; // only one
    std::vector<struct pollfd> pollfds;
    VfioConsumer *callback_context;
    std::set<void*> mapped;
    std::map<void*, dma_sg_t*> sgs;

    VfioUserServer(std::string sock) {
      this->sock = sock;
    }

    ~VfioUserServer() {
      int ret;
      vfu_destroy_ctx(vfu_ctx); // this should also close this->run_ctx_pollfd_idx.value()
      
      // close remaining fds
      for (auto fd : this->pollfds) {
        if (fd.fd == this->get_run_ctx_poll_fd()->fd) {
          continue;
        }
        ret = close(fd.fd);
        if (ret < 0)
          warn("Cleanup: Cannot close fd %d", fd.fd);
      }

      // if vfu_destroy_ctx is successfull, this might not be needed
      ret = unlink(sock.c_str());
      if (ret < 0) {
        warn("Cleanup: Could not delete %s", sock.c_str());
      }
    }

    int add_regions(std::vector<struct vfio_region_info> regions, int device_fd) {
      int ret;

      if (regions.size() > VFU_PCI_DEV_VGA_REGION_IDX - VFU_PCI_DEV_BAR0_REGION_IDX + 1)
        printf("Warning: got %u mappable regions, but we expect normal PCI to have %d at most\n", (uint)regions.size(), VFU_PCI_DEV_VGA_REGION_IDX - VFU_PCI_DEV_BAR0_REGION_IDX + 1);
      // Note, that we only attempt to map bar 0-5
      for (int i = VFU_PCI_DEV_BAR0_REGION_IDX; i <= VFU_PCI_DEV_BAR5_REGION_IDX; i++) {
        ret = this->add_region(&regions[i], device_fd);
        if (ret < 0) {
            die("failed to add dma region to vfio-user");
        }
      }

      return 0;
    }

    int add_irqs(std::vector<struct vfio_irq_info> const irqs) {
      int ret;
      if (irqs.size() > VFU_DEV_REQ_IRQ - VFU_DEV_INTX_IRQ + 1)
        printf("Warning: got %u irq types, but we only know %d at most\n", (uint)irqs.size(), VFU_DEV_REQ_IRQ - VFU_DEV_INTX_IRQ + 1);
      for (int i = VFU_DEV_INTX_IRQ; i <= VFU_DEV_REQ_IRQ; i++) {
        // TODO check ioeventfd compatibility
        ret = vfu_setup_device_nr_irqs(this->vfu_ctx, (enum vfu_dev_irq_type)irqs[i].index, irqs[i].count);
        if (ret < 0) {
          die("Cannot set up vfio-user irq (type %d, num %d)", irqs[i].index, irqs[i].count);
        }

        int fd = eventfd(0, 0); // TODO close
        if (fd < 0) {
          die("Cannot create eventfd for irq.");
        }
        //ret = vfu_create_ioeventfd(this->vfu_ctx, 
        printf("Interrupt (type %d, num %d) set up.\n", irqs[i].index, irqs[i].count);
      }

      return 0;
    }

    void add_msix_pollfds(std::vector<int> const eventfds) {
      this->irq_msix_pollfd_idx = this->pollfds.size();
      this->irq_msix_pollfd_count = eventfds.size();
      for (auto eventfd : eventfds) {
        auto pfd = (struct pollfd) {
            .fd = eventfd,
            .events = POLLIN
            };
        this->pollfds.push_back(pfd);
      }
      if (!eventfds.empty())
        printf("registered fds %d-%d to poll for msix interrupts\n", 
            eventfds.front(), eventfds.back());
    }

    void add_legacy_irq_pollfds(const int intx, const int msi, const int err, const int req) {
      struct pollfd pfd; 

      this->irq_intx_pollfd_idx = this->pollfds.size();
      pfd = (struct pollfd) { .fd = intx, .events = POLLIN };
      this->pollfds.push_back(pfd);

      this->irq_msi_pollfd_idx = this->pollfds.size();
      pfd = (struct pollfd) { .fd = msi, .events = POLLIN };
      this->pollfds.push_back(pfd);

      this->irq_err_pollfd_idx = this->pollfds.size();
      pfd = (struct pollfd) { .fd = err, .events = POLLIN };
      this->pollfds.push_back(pfd);

      this->irq_req_pollfd_idx = this->pollfds.size();
      pfd = (struct pollfd) { .fd = req, .events = POLLIN };
      this->pollfds.push_back(pfd);

      printf("register interrupt fds: intx %d, msi %d, err %d, req %d\n", intx, msi, err, req);
    }

    void reset_run_ctx_poll_fd() {
      auto pfd = (struct pollfd) {
          .fd = vfu_get_poll_fd(vfu_ctx),
          .events = POLLIN
          };
      if (this->run_ctx_pollfd_idx.has_value()) {
        close(this->pollfds[this->run_ctx_pollfd_idx.value()].fd);
        this->pollfds[this->run_ctx_pollfd_idx.value()] = pfd;
      } else {
        this->run_ctx_pollfd_idx = this->pollfds.size();
        this->pollfds.push_back(pfd);
      }
    }

    struct pollfd* get_run_ctx_poll_fd() {
        return &this->pollfds[this->run_ctx_pollfd_idx.value()];
    }

    void setup_callbacks(VfioConsumer *callback_context) {
      int ret;
      this->callback_context = callback_context;
      // I think quiescing only applies when using vfu_add_to_sgl and vfu_sgl_read (see libvfio-user/docs/memory-mapping.md
      //vfu_setup_device_quiesce_cb(this->vfu_ctx, VfioUserServer::quiesce_cb);
      ret = vfu_setup_device_reset_cb(this->vfu_ctx, VfioUserServer::reset_device_cb);
      if (ret)
        die("setting up reset callback for libvfio-user failed %d", ret);
      vfu_setup_device_dma(this->vfu_ctx, VfioUserServer::dma_register_cb, VfioUserServer::dma_unregister_cb);
      ret = vfu_setup_irq_state_callback(this->vfu_ctx, VFU_DEV_INTX_IRQ, VfioUserServer::intx_state_cb);
      if (ret)
        die("setting up intx state callback for libvfio-user failed");
      ret = vfu_setup_irq_state_callback(this->vfu_ctx, VFU_DEV_MSIX_IRQ, VfioUserServer::msix_state_cb);
      if (ret)
        die("setting up msix state callback for libvfio-user failed");
      // register unimplemented callback for all unused interrupt types
      for (int type = 0; type < VFU_DEV_NUM_IRQS; type++) {
        if (type == VFU_DEV_INTX_IRQ || type == VFU_DEV_MSIX_IRQ)
          continue;
        ret = vfu_setup_irq_state_callback(this->vfu_ctx, (enum vfu_dev_irq_type) type, VfioUserServer::irq_state_unimplemented_cb);
        if (ret)
          die("setting up irq type %d callback for libvfio-user failed", type);
      }
    }

  private:
    static int reset_device_cb(vfu_ctx_t *vfu_ctx, [[maybe_unused]] vfu_reset_type_t type) {
      VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      printf("resetting device\n"); // this happens at VM boot
      vfu->callback_context->reset_device();
      return 0;
    }

    static int quiesce_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx) {
      // See vfu_setup_device_quiesce_cb().
      //VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      //vfu_quiesce_done(vfu.vfu_ctx, 0);
      //die("quiesce_cb unimplemented");
      printf("quiescing device. Not sure when this ends.\n");
      return 0;
    }

    static void dma_register_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] vfu_dma_info_t *info) {
      
      printf("register dma cb\n");
      
      //if ( //info->iova.iov_base == NULL ||
      //    info->iova.iov_base == (void*)0xc0000 || // TODO remove these checks as well
      //    info->iova.iov_base == (void*)0xe0000 )
      //  return;

      //__builtin_dump_struct(info, &printf);
      VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      printf("{\n");
      for(void* const& p: vfu->mapped){
        printf("%p\n",p);
      }
      printf("}\n");
      uint32_t flags = 0;
      if (PROT_READ & info->prot)
        flags |= VFIO_DMA_MAP_FLAG_READ;
      if (PROT_WRITE & info->prot)
        flags |= VFIO_DMA_MAP_FLAG_WRITE;
      __builtin_dump_struct(info, &printf);
  
      if (!info->vaddr){
        //vfu_sgl_get failes if vaddr is NULL
        printf("Region not mappable\n");
        return;
      }

      //Map the region into the vmux Address Space
      struct iovec* mapping = (struct iovec*)calloc(1,sizeof(struct iovec));
      dma_sg_t *sgl = (dma_sg_t*)calloc(1, dma_sg_size());
      int ret = vfu_addr_to_sgl(vfu_ctx,info->iova.iov_base,info->iova.iov_len,sgl,1,flags);
      if(ret < 0){
        die("Failed to get sgl for DMA\n");
      }
  
      if(vfu_sgl_get(vfu_ctx ,sgl,mapping,1,0)){
        die("Failed to populate iovec array");
      }

      


      printf("Add Address to mapped addresses\n");
      vfu->sgs[info->vaddr] = sgl; 
      vfu->mapped.insert(info->vaddr);
      __builtin_dump_struct(info, &printf);
      __builtin_dump_struct(mapping, &printf);
      
      
      vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(vfio_iommu_type1_dma_map),
        .flags = flags,
        .vaddr = (uint64_t)(info->vaddr),
        .iova = (uint64_t)(info->iova.iov_base),
        .size =  info->iova.iov_len,
      };
      __builtin_dump_struct(&dma_map, &printf);
      vfu->callback_context->map_dma(&dma_map);
    }

    static void dma_unregister_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] vfu_dma_info_t *info) {
      printf("dma_unregister_cb\n");
      //return;

      //if (// info->iova.iov_base == NULL ||
      //    info->iova.iov_base == (void*)0xc0000 ||
      //    info->iova.iov_base == (void*)0xe0000 )
      //  return;
      __builtin_dump_struct(info, &printf);

      VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      printf("{\n");
      for(void* const& p: vfu->mapped){
        printf("%p\n",p);
      }
      printf("}\n");


      //info->mapping indicates if mapped
      if(!info->mapping.iov_base){
        printf("Region not mapped, nothing to do\n");
        return;
      }


      if(!vfu->mapped.count(info->iova.iov_base)){
        printf("Region seems not to be mapped\n");
        //return;
      }
      if(vfu->sgs.count(info->vaddr)==1){
        printf("dealloc sgl\n");
      }
      if(vfu->sgs.count(info->vaddr)>1){
        printf("Why?\n");
        //return;
      }


      vfio_iommu_type1_dma_unmap dma_unmap = {
        .argsz = sizeof(vfio_iommu_type1_dma_unmap),
        .flags = 0,
        .iova = (uint64_t)(info->iova.iov_base),
        .size = info->iova.iov_len,
      };
      vfu->callback_context->unmap_dma(&dma_unmap);
    }

    static void intx_state_cb(vfu_ctx_t *vfu_ctx, uint32_t start, uint32_t count, bool mask) {
      VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      vfu->callback_context->mask_irqs(VFIO_PCI_INTX_IRQ_INDEX, start, count, mask);
    }

    static void msix_state_cb(vfu_ctx_t *vfu_ctx, uint32_t start, uint32_t count, bool mask) {
      VfioUserServer *vfu = (VfioUserServer*)vfu_get_private(vfu_ctx);
      vfu->callback_context->mask_irqs(VFIO_PCI_MSIX_IRQ_INDEX, start, count, mask);
    }

    static void irq_state_unimplemented_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] uint32_t start, [[maybe_unused]] uint32_t count, [[maybe_unused]] bool mask) {
      die("irq_state_unimplemented_cb unimplemented");
    }

    static ssize_t
    unexpected_access_callback([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] char * const buf, [[maybe_unused]] size_t count, [[maybe_unused]] __loff_t offset,
                [[maybe_unused]] const bool is_write)
    {
      printf("Unexpectedly, a vfio register/DMA access callback was triggered (at 0x%lx, is write %d.\n", offset, is_write);
      return 0;
    }

    int add_region(struct vfio_region_info *region, int device_fd) {
      int ret;

      if (region->size == 0) {
        printf("Bar region %d skipped.\n", region->index);
        return 0;
      }

      // set up dma VM<->vmux
      struct iovec bar_mmap_areas[] = {
          //{ .iov_base  = (void*)region->offset, .iov_len = region->size}, // no cb, just shmem
          { .iov_base  = (void*)0, .iov_len = region->size}, // no cb, just shmem
      };
      ret = vfu_setup_region(this->vfu_ctx, region->index,
                             region->size, &(this->unexpected_access_callback),
                             VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM, bar_mmap_areas, 
                             1, // nr. items in bar_mmap_areas
                             device_fd, region->offset);
      if (ret < 0) {
          die("failed to setup BAR region %d", region->index);
      }

      // init some flags that are also set with qemu passthrough
      vfu_pci_config_space_t *config_space = vfu_pci_get_config_space(this->vfu_ctx);
      vfu_bar_t *bar_config = &(config_space->hdr.bars[region->index]);
      // see pci spec sec 7.5.1.2.1 for meaning of bits:
      bar_config->mem.prefetchable = 1; // prefetchable
      bar_config->mem.locatable = 0b10; // 64 bit

      printf("Vfio-user: Bar region %d (offset 0x%x, size 0x%x) set up.\n", region->index, (uint)region->offset, (uint)region->size);

      return 0;
    }
};


std::string get_iommu_group(std::string pci_device){
  std::string path = "/sys/kernel/iommu_groups/";
  struct dirent *iommu_group;
  DIR *iommu_dir = opendir(path.c_str());
  if (iommu_dir == NULL){
          return "";
  }
  while((iommu_group = readdir(iommu_dir)) != NULL) {
          if(strcmp(iommu_group->d_name,".") != 0 && strcmp(iommu_group->d_name,"..") != 0){
                  std::string iommu_group_str = iommu_group->d_name;
                  struct dirent *iommu_group_dir;
                  DIR *pci = opendir((path + iommu_group->d_name + "/devices").c_str());
                  while((iommu_group_dir = readdir(pci)) != NULL){
                          if(pci_device == iommu_group_dir->d_name){
                            closedir(pci);
                            closedir(iommu_dir);
                            return iommu_group_str;
                          }
                                    

                  }
                  closedir(pci);
          }
  }
  closedir(iommu_dir);
  return "";
}

std::vector<int>  get_hardware_ids(std::string pci_device,std::string iommu_group){
  std::string path = "/sys/kernel/iommu_groups/" + iommu_group +"/devices/" + pci_device + "/";
  std::vector<std::string> values = {"revision", "vendor", "device", "subsystem_vendor", "subsystem_device"};
  std::vector<int> result;
  int bytes_read;
  char id_buffer[7];
  FILE* id;
  
  for(size_t i = 0; i < values.size(); i++ ){
    id = fopen((path + values[i]).c_str(), "r");
    if(id == NULL){
      result.clear();
      printf("Failed ot open %s\n",(path + values[i]).c_str());
      return result;
    }
    bytes_read = fread(id_buffer, 1, sizeof(id_buffer) / sizeof(id_buffer[0]) - 1, id);
    if(bytes_read < 1){
      result.clear();
      printf("Failed to read %s, got %s\n", values[i].c_str(),id_buffer);
      return result;
    }
    result.push_back((int)strtol(id_buffer,NULL,0));
    fclose(id);
  }   

  return result;
}



int _main(int argc, char** argv) {
  int ret;

  int ch;
  std::string device = "0000:18:00.0";
  std::string group_arg;
  int HARDWARE_REVISION; // could be set by vfu_pci_set_class: vfu_ctx->pci.config_space->hdr.rid = 0x02;
  std::vector<int> pci_ids;
  std::string socket = "/tmp/vmux.sock";
  while ((ch = getopt(argc,argv,"hd:s:")) != -1){
    switch(ch)
      {
      case 'd':
        device = optarg;
        break;
      case 's':
        socket = optarg;
        break;
      case '?':
      case 'h':
        std::cout << "-d 0000:18:00.0                        PCI-Device\n"
                  << "-s /tmp/vmux.sock                      Path of the socket\n";
        return 0;
      default:
        break;
      }
  }
  //Get IOMMU Group from the PCI device
  group_arg = get_iommu_group(device);
  if(group_arg == ""){
    printf("Failed to map PCI device %s to IOMMU-Group\n",device.c_str());
    return -1;
  }
  //Get Hardware Information from Device
  pci_ids = get_hardware_ids(device,group_arg);
  if(pci_ids.size() != 5){
    printf("Failed to parse Hardware Information, expected %d IDs got %zu\n",5,pci_ids.size());
    return -1;
  }
  HARDWARE_REVISION = pci_ids[0];
  pci_ids.erase(pci_ids.begin()); //Only contains Vendor ID, Device ID, Subsystem Vendor ID, Subsystem ID now

  printf("PCI-Device: %s\nIOMMU-Group: %s\nRevision: 0x%02X\nIDs: 0x%04X,0x%04X,0x%04X,0x%04X\nSocket: %s\n",
        device.c_str(),
        group_arg.c_str(),
        HARDWARE_REVISION,
        pci_ids[0],pci_ids[1],pci_ids[2],pci_ids[3],
        socket.c_str());

  printf("hello 0x%X, %d, \n", VFIO_DEVICE_STATE_V1_RESUMING, VFIOC_SECRET);

  VfioUserServer vfu = VfioUserServer(socket);

  // init vfio
  
  VfioConsumer vfioc(group_arg,device);

  ret = vfioc.init();
  if (ret < 0) {
    die("failed to initialize vfio consumer");
  }
  
  ret = vfioc.init_mmio();
  if (ret < 0) {
    die("failed to initialize vfio mmio mappings");
  }
  printf("Test read: 0x%x\n", *(uint32_t*)((char*)(vfioc.mmio[VFU_PCI_DEV_BAR0_REGION_IDX]) + 0x83048));

  // init vfio-user

  vfu.vfu_ctx = vfu_create_ctx(
    VFU_TRANS_SOCK,
    vfu.sock.c_str(),
    0,
    &vfu,
    VFU_DEV_TYPE_PCI
  );
  if (vfu.vfu_ctx == NULL) {
    die("failed to initialize device emulation");
  }
  
  ret = vfu_setup_log(vfu.vfu_ctx, _log, LOG_DEBUG);
  if (ret < 0) {
    die("failed to setup log");
  }

  ret = vfu_pci_init(vfu.vfu_ctx, VFU_PCI_TYPE_EXPRESS,
                     PCI_HEADER_TYPE_NORMAL, 0); // maybe 4?
  if (ret < 0) {
    die("vfu_pci_init() failed") ;
  }

  vfu_pci_set_id(vfu.vfu_ctx, pci_ids[0], pci_ids[1], pci_ids[2], pci_ids[3]);
  vfu_pci_config_space_t *config_space = vfu_pci_get_config_space(vfu.vfu_ctx);
  config_space->hdr.rid = HARDWARE_REVISION;
  vfu_pci_set_class(vfu.vfu_ctx, 0x02, 0x00, 0x00);

  // set up vfio-user DMA

  ret = vfu.add_regions(vfioc.regions, vfioc.device);
  if (ret < 0)
    die("failed to add regions");

  // set up irqs 

  ret = vfu.add_irqs(vfioc.interrupts);
  if (ret < 0)
    die("failed to add irqs");
  
  vfioc.init_legacy_irqs();
  vfu.add_legacy_irq_pollfds(vfioc.irqfd_intx, vfioc.irqfd_msi, vfioc.irqfd_err, vfioc.irqfd_req);
  vfioc.init_msix();
  vfu.add_msix_pollfds(vfioc.irqfds);

  // set capabilities
  if(vfioc.is_pcie){
    Capabilities caps = Capabilities(&(vfioc.regions[VFU_PCI_DEV_CFG_REGION_IDX]), device);
    void *cap_data;

    cap_data = caps.pm();
    ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, 0, cap_data);
    if (ret < 0)
      die("add cap error");
    free(cap_data);

    cap_data = caps.msix();
    ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, 0, cap_data);
    if (ret < 0)
      die("add cap error");
    free(cap_data);

    // not supported by libvfio-user:
    //cap_data = caps.msi();
    //ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, VFU_CAP_FLAG_READONLY, cap_data);
    //if (ret < 0)
    //  die("add cap error");
    //free(cap_data);
    
    cap_data = caps.exp();
    ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, VFU_CAP_FLAG_READONLY, cap_data);
    if (ret < 0)
      die("add cap error");
    free(cap_data);
  
    // not supported by upstream libvfio-user, not used by linux ice driver
    //cap_data = caps.vpd();
    //ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, VFU_CAP_FLAG_READONLY, cap_data);
    //if (ret < 0)
    //  die("add cap error");
    //free(cap_data);
    
    cap_data = caps.dsn();
    ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, VFU_CAP_FLAG_READONLY | VFU_CAP_FLAG_EXTENDED, cap_data);
    if (ret < 0)
      die("add cap error");
    free(cap_data);

    // see lspci about which fields mean what https://github.com/pciutils/pciutils/blob/42e6a803bda392e98276b71994db0b0dd285cab1/ls-caps.c#L1469
    //struct msixcap data;
    //data.hdr = {
    //  .id = PCI_CAP_ID_MSIX,
    //};
    //data.mxc = {
    //  // message control
    //};
    //data.mtab = {
    //  // table offset / Table BIR
    //};
    //data.mpba = {
    //  // PBA offset / PBA BIR
    //};
    ////ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, 0, &data);
    //if (ret < 0) {
    //  die("Vfio-user: cannot add MSIX capability");
    //}
  }
  // register callbacks

  //int reset_device(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type) {
  //vfu_setup_device_reset_cb(vfu.vfu_ctx, vfioc.reset_device);
  vfu.setup_callbacks(&vfioc);

  // finalize

  ret = vfu_realize_ctx(vfu.vfu_ctx);
  if (ret < 0) {
      die("failed to realize device");
  }

  printf("Waiting for qemu to attach...\n");

  ret = vfu_attach_ctx(vfu.vfu_ctx);
  if (ret < 0) {
      die("failed to attach device");
  }

  vfu.reset_run_ctx_poll_fd();

  // runtime loop
  
  // in our case this is msix (type 2) interrupts (0-1023)
  //ret = vfu_irq_trigger(vfu.vfu_ctx, 0xFFFFFFFF);

  do {
    ret = poll(vfu.pollfds.data(), vfu.pollfds.size(), 500);
    if (ret < 0) {
      die("failed to poll(2)");
    }

    // check for interrupts to pass on
    struct pollfd *pfd = &(vfu.pollfds[vfu.irq_intx_pollfd_idx]);
    if (pfd->revents & POLLIN) {
      printf("intx interrupt! unimplemented\n");
    }
    pfd = &(vfu.pollfds[vfu.irq_msi_pollfd_idx]);
    if (pfd->revents & POLLIN) {
      printf("msi interrupt! unimplemented\n");
    }
    pfd = &(vfu.pollfds[vfu.irq_err_pollfd_idx]);
    if (pfd->revents & POLLIN) {
      printf("err interrupt! unimplemented\n");
    }
    pfd = &(vfu.pollfds[vfu.irq_req_pollfd_idx]);
    if (pfd->revents & POLLIN) {
      printf("req interrupt! unimplemented\n");
    }
    for (uint64_t i = vfu.irq_msix_pollfd_idx; i < vfu.irq_msix_pollfd_idx + vfu.irq_msix_pollfd_count; i++) {
      struct pollfd *pfd = &(vfu.pollfds[i]);
      if (pfd->revents & (POLLIN)) {
        // pass on (trigger) interrupt
        size_t irq_subindex = i - vfu.irq_msix_pollfd_idx;
        ret = vfu_irq_trigger(vfu.vfu_ctx, irq_subindex);
        printf("Triggered interrupt. ret = %d\n", ret);
        if (ret < 0) {
          die("Cannot trigger MSIX interrupt %lu", irq_subindex);
        }
        break;
      }
    }

    // continue running
    if (vfu.get_run_ctx_poll_fd()->revents & POLLIN) {
      ret = vfu_run_ctx(vfu.vfu_ctx);
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
  } while (!quit.load());

  // destruction is done by ~VfioUserServer

  return 0;
}

void signal_handler(int) {
  quit.store(true);
}

int main(int argc, char** argv) {
  // register signal handler to handle SIGINT gracefully to call destructors
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);

  try {
    return _main(argc, argv);
  } catch (...) {
    // we seem to need this catch everyting so that our destructors work
    return EXIT_FAILURE;
  }

  return quit;
}

