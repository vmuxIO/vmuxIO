#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

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
_log(vfu_ctx_t *vfu_ctx, int level, char const *msg)
{
    fprintf(stderr, "server[%d]: %s\n", getpid(), msg);
}

static ssize_t
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

int main() {
  int ret;
  char tmpfilename[] = "/tmp/libvfio-user.XXXXXX";
  int tmpfd;
  vfu_ctx_t *vfu_ctx;
  vmux_dev_ctx_t dev_ctx = { 
    .value = { 0x1, 0x0 }
  };

  printf("hello 0x%X, %d, \n", VFIO_DEVICE_STATE_V1_RESUMING, VFIOC_SECRET);

  // init vfio
  
  vfio_consumer_t vfioc;
  vfioc_init(&vfioc);
  /*return 0;*/

  // init vfio-user

  vfu_ctx = vfu_create_ctx(
    VFU_TRANS_SOCK,
    "/tmp/peter.sock",
    0,
    &dev_ctx,
    VFU_DEV_TYPE_PCI
  );
  if (vfu_ctx == NULL) {
    err(EXIT_FAILURE, "failed to initialize device emulation");
  }
  
  ret = vfu_setup_log(vfu_ctx, _log, LOG_DEBUG);
  if (ret < 0) {
    err(EXIT_FAILURE, "failed to setup log");
  }

  ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_EXPRESS, // TODO express
                     PCI_HEADER_TYPE_NORMAL, 0); // TODO 4?
  if (ret < 0) {
    err(EXIT_FAILURE, "vfu_pci_init() failed") ;
  }

  vfu_pci_set_id(vfu_ctx, 0xdead, 0xbeef, 0xcafe, 0xbabe);

  // bar 0: mem (iomem)

  ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX, sizeof(dev_ctx.value),
      &bar0_access, VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM, NULL, 0, -1, 0);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to setup BAR0 region");
  }

  // bar 1: PIO (ioport), with shared vmux memory
  
  // create file backing the memory and map it
  umask(0022);
  if ((tmpfd = mkstemp(tmpfilename)) == -1) {
      err(EXIT_FAILURE, "failed to create backing file");
  }
  unlink(tmpfilename);
  if (ftruncate(tmpfd, sizeof(dev_ctx.value)) == -1) {
      err(EXIT_FAILURE, "failed to truncate backing file");
  }
  dev_ctx.bar1size = 0x2000;
  dev_ctx.bar1 = mmap(NULL, dev_ctx.bar1size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, tmpfd, 0);
  *((uint64_t*)(dev_ctx.bar1)) = 0x2;
  if (dev_ctx.bar1 == MAP_FAILED) {
      err(EXIT_FAILURE, "failed to mmap BAR1");
  }
  // set it up
  struct iovec bar1_mmap_areas[] = {
      { .iov_base  = (void*)0, .iov_len = 0x1000}, // no cb, just shmem from here
      // 0x1000 - 0x2000 will be handled by bar0_access callback
  };
  ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR1_REGION_IDX,
                         dev_ctx.bar1size, &bar0_access,
                         VFU_REGION_FLAG_RW, bar1_mmap_areas, 1,
                         tmpfd, 0);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to setup BAR1 region");
  }

  // finalize

  ret = vfu_realize_ctx(vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to realize device");
  }

  ret = vfu_attach_ctx(vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to attach device");
  }

  // runtime loop

  do {
    ret = vfu_run_ctx(vfu_ctx);
    if (ret == -1 && errno == EINTR) {
      // interrupt happend during run and wants to be processed
    }
  } while (ret == 0);

  if (ret == -1 &&
      errno != ENOTCONN && errno != EINTR && errno != ESHUTDOWN) {
      errx(EXIT_FAILURE, "failed to realize device emulation");
  }

  // destruction

  vfu_destroy_ctx(vfu_ctx);
  return 0;
}
