#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include <string>

#include "src/vfio-consumer.hpp"

extern "C" {
  #include "libvfio-user.h"
}

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
    std::string tmpfilename = "/tmp/libvfio-user.XXXXXX";
    std::string sock = "/tmp/peter.sock";
    // maps indexed by bar index:
    std::map<int, void*> mem_pages;
    std::map<int, int> shm_fds;
   
    VfioUserServer() {
    }

    int add_regions(std::vector<struct vfio_region_info> regions) {
      int ret;

      if (regions.size() > VFU_PCI_DEV_VGA_REGION_IDX - VFU_PCI_DEV_BAR0_REGION_IDX)
        printf("Warning: got %u mappable regions, but we expect normal PCI to have %d at most", (uint)regions.size(), VFU_PCI_DEV_VGA_REGION_IDX - VFU_PCI_DEV_BAR0_REGION_IDX);
      for (int i = VFU_PCI_DEV_BAR0_REGION_IDX; i <= VFU_PCI_DEV_VGA_REGION_IDX; i++) {
        ret = this->add_region(&regions[i]);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to add dma region to vfio-user");
        }
      }

      return 0;
    }

  private:
    static ssize_t
    unexpected_access_callback([[maybe_unused]] vfu_ctx_t *vfu_ctx, [[maybe_unused]] char * const buf, [[maybe_unused]] size_t count, [[maybe_unused]] __loff_t offset,
                [[maybe_unused]] const bool is_write)
    {
      printf("Unexpectedly, a vfio register/DMA access callback was triggered.");
      return 0;
    }

    int add_region(struct vfio_region_info *region) {
      int ret;

      if (region->size == 0) {
        return -1;
      }

      // create shared memory (file backing the memory and map it)
      int tmpfd;
      umask(0022);
      if ((tmpfd = mkstemp(this->tmpfilename.data())) == -1) {
          err(EXIT_FAILURE, "failed to create backing file");
      }
      this->shm_fds[region->index] = tmpfd;
      unlink(this->tmpfilename.c_str());
      if (ftruncate(tmpfd, sizeof(region->size)) == -1) {
          err(EXIT_FAILURE, "failed to truncate backing file");
      }
      void* sharable_page = mmap(NULL, region->size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, tmpfd, 0);
      this->mem_pages[region->index] = sharable_page;
      if (sharable_page == MAP_FAILED) {
          err(EXIT_FAILURE, "failed to mmap BAR %d", region->index);
      }

      // set up dma VM<->vmux
      struct iovec bar_mmap_areas[] = {
          { .iov_base  = (void*)0, .iov_len = region->size}, // no cb, just shmem
      };
      ret = vfu_setup_region(this->vfu_ctx, region->index,
                             region->size, &(this->unexpected_access_callback),
                             VFU_REGION_FLAG_RW, bar_mmap_areas, 
                             1, // nr. items in bar_mmap_areas
                             tmpfd, 0);
      if (ret < 0) {
          err(EXIT_FAILURE, "failed to setup BAR region %d", region->index);
      }

      printf("Bar region %d (size 0x%x) set up.", region->index, (uint)region->size);

      return 0;
    }
};

int main() {
  int ret;

  printf("hello 0x%X, %d, \n", VFIO_DEVICE_STATE_V1_RESUMING, VFIOC_SECRET);

  VfioUserServer vfu = VfioUserServer();

  // init vfio
  
  VfioConsumer vfioc;
  ret = vfioc.init();
  if (ret < 0)
    err(EXIT_FAILURE, "failed to initialize vfio consumer");
  /*return 0;*/

  // init vfio-user

  vfu.vfu_ctx = vfu_create_ctx(
    VFU_TRANS_SOCK,
    vfu.sock.c_str(),
    0,
    &vfu,
    VFU_DEV_TYPE_PCI
  );
  if (vfu.vfu_ctx == NULL) {
    err(EXIT_FAILURE, "failed to initialize device emulation");
  }
  
  ret = vfu_setup_log(vfu.vfu_ctx, _log, LOG_DEBUG);
  if (ret < 0) {
    err(EXIT_FAILURE, "failed to setup log");
  }

  ret = vfu_pci_init(vfu.vfu_ctx, VFU_PCI_TYPE_EXPRESS, // TODO express
                     PCI_HEADER_TYPE_NORMAL, 0); // TODO 4?
  if (ret < 0) {
    err(EXIT_FAILURE, "vfu_pci_init() failed") ;
  }

  vfu_pci_set_id(vfu.vfu_ctx, 0xdead, 0xbeef, 0xcafe, 0xbabe);

  // set up DMA

  ret = vfu.add_regions(vfioc.regions);
  if (ret < 0)
    err(EXIT_FAILURE, "failed to add regions");

  // finalize

  ret = vfu_realize_ctx(vfu.vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to realize device");
  }

  ret = vfu_attach_ctx(vfu.vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to attach device");
  }

  // runtime loop

  do {
    ret = vfu_run_ctx(vfu.vfu_ctx);
    if (ret == -1 && errno == EINTR) {
      // interrupt happend during run and wants to be processed
    }
  } while (ret == 0);

  if (ret == -1 &&
      errno != ENOTCONN && errno != EINTR && errno != ESHUTDOWN) {
      errx(EXIT_FAILURE, "failed to realize device emulation");
  }

  // destruction

  vfu_destroy_ctx(vfu.vfu_ctx);
  return 0;
}
