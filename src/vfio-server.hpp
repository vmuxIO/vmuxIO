#pragma once

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
#include <signal.h>
#include "src/vfio-consumer.hpp"
#include "src/util.hpp"
#include "src/caps.hpp"
#include <thread>
#include <sys/epoll.h>
extern "C" {
  #include "libvfio-user.h"
}

#include "util.hpp"

class VfioUserServer;

struct interrupt_callback{
  int fd;
  void (*callback)(int, VfioUserServer*);
  VfioUserServer* vfu;
  int irq_subindex;
};


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

    int efd;

    interrupt_callback ic[1028];
    size_t ic_used;


    VfioUserServer(std::string sock, int efd) {
      this->sock = sock;
      this->efd = efd;
      ic_used = 0;
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

    static void msix_callback(int fd, VfioUserServer* vfu){

      //size_t irq_subindex = i - vfu.irq_msix_pollfd_idx;
      int ret = vfu_irq_trigger(vfu->vfu_ctx, fd);
      printf("Triggered interrupt. ret = %d, errno: %d\n", ret,errno);
      if (ret < 0) {
        die("Cannot trigger MSIX interrupt %d", fd);
      }


    }
    static void msi_callback(int fd, VfioUserServer* vfu){
      (void)vfu;
      (void)fd;
      printf("msi interrupt! unimplemented\n");
    }

    static void intx_callback(int fd, VfioUserServer* vfu){
      (void)vfu;
      (void)fd;
      printf("intx interrupt! unimplemented\n");
    }

    static void err_callback(int fd, VfioUserServer* vfu){
      (void)vfu;
      (void)fd;
      printf("err interrupt! unimplemented\n");
    }

    static void req_callback(int fd, VfioUserServer* vfu){
      (void)vfu;
      (void)fd;
      printf("req interrupt! unimplemented\n");
    }




    void add_msix_pollfds(std::vector<int> const eventfds) {

      
      this->irq_msix_pollfd_idx = this->pollfds.size();
      this->irq_msix_pollfd_count = eventfds.size();
      
      for(size_t i = 0; i < eventfds.size(); i++){
        ic[ic_used].fd = eventfds[i];
        ic[ic_used].callback = msix_callback;
        ic[ic_used].vfu = this;
        struct epoll_event e;
        e.events = EPOLLIN;
        e.data.ptr = &ic[ic_used++];
        
        epoll_ctl(efd,EPOLL_CTL_ADD, eventfds[i], &e);

      }
      if (!eventfds.empty())
        printf("registered fds %d-%d to poll for msix interrupts\n", 
            eventfds.front(), eventfds.back());
    }

    void add_legacy_irq_pollfds(const int intx, const int msi, const int err, const int req) {
        struct epoll_event e;

        ic[ic_used].fd = intx;
        ic[ic_used].callback = intx_callback;
        ic[ic_used].vfu = this;
        
        e.events = EPOLLIN;
        e.data.ptr = &ic[ic_used++];
        
        epoll_ctl(efd,EPOLL_CTL_ADD, intx, &e);

        ic[ic_used].fd = msi;
        ic[ic_used].callback = msi_callback;
        ic[ic_used].vfu = this;
        
        e.events = EPOLLIN;
        e.data.ptr = &ic[ic_used++];       

        epoll_ctl(efd,EPOLL_CTL_ADD, msi, &e); 

        ic[ic_used].fd = err;
        ic[ic_used].callback = err_callback;
        ic[ic_used].vfu = this;
        
        e.events = EPOLLIN;
        e.data.ptr = &ic[ic_used++];       

        epoll_ctl(efd,EPOLL_CTL_ADD, err, &e); 

        ic[ic_used].fd = req;
        ic[ic_used].callback = req_callback;
        ic[ic_used].vfu = this;
        
        e.events = EPOLLIN;
        e.data.ptr = &ic[ic_used++];       

        epoll_ctl(efd,EPOLL_CTL_ADD, req, &e); 

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

    static void msix_state_cb([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] uint32_t start, [[maybe_unused]] uint32_t count, [[maybe_unused]] bool mask) {
      // masking of MSIx interrupts is unimplemented in linux/vfio (see vfio_pci_intrs.c:666)
      printf("ignoring msix state cb (mask %u)\n", mask);
      // TODO track maskedness of interrupts and make vmux apply the mask
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