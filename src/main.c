#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>

#include "libvfio-user.h"

typedef struct {
  uint64_t value[2];
} vmux_dev_ctx_t;

static void
_log(vfu_ctx_t *vfu_ctx, int level, char const *msg)
{
    fprintf(stderr, "server[%d]: %s\n", getpid(), msg);
}

static ssize_t
bar0_access(vfu_ctx_t *vfu_ctx, char * const buf, size_t count, loff_t offset,
            const bool is_write)
{
  vmux_dev_ctx_t *dev_ctx = vfu_get_private(vfu_ctx);

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
  vfu_ctx_t *vfu_ctx;
  vmux_dev_ctx_t dev_ctx = { .value[0] = 0x1 };

  printf("hello 0x%X\n", VFIO_DEVICE_STATE_V1_RESUMING);

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

  ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL, // TODO express
                     PCI_HEADER_TYPE_NORMAL, 0); // TODO 4?
  if (ret < 0) {
    err(EXIT_FAILURE, "vfu_pci_init() failed") ;
  }

  vfu_pci_set_id(vfu_ctx, 0xdead, 0xbeef, 0xcafe, 0xbabe);

  ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX, sizeof(dev_ctx.value),
                         &bar0_access, VFU_REGION_FLAG_RW, NULL, 0, -1, 0);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to setup BAR0 region");
  }

  ret = vfu_realize_ctx(vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to realize device");
  }

  ret = vfu_attach_ctx(vfu_ctx);
  if (ret < 0) {
      err(EXIT_FAILURE, "failed to attach device");
  }

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

  vfu_destroy_ctx(vfu_ctx);
  return 0;
}
