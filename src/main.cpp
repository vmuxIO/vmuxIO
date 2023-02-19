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
    std::string sock = "/tmp/peter.sock";
    std::optional<size_t> run_ctx_pollfd_idx; // index of pollfd in pollfds
    size_t irq_pollfd_idx;
    size_t irq_pollfd_count;
    std::vector<struct pollfd> pollfds;
   
    VfioUserServer() {
    }

    ~VfioUserServer() {
      int ret;
      vfu_destroy_ctx(vfu_ctx); // this should also close this->run_ctx_pollfd_idx.value()
      
      // close remaining fds
      for (auto fd : this->pollfds) {
        if (fd.fd == this->get_poll_fd()->fd) {
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
      this->irq_pollfd_idx = this->pollfds.size();
      this->irq_pollfd_count = eventfds.size();
      for (auto eventfd : eventfds) {
        auto pfd = (struct pollfd) {
            .fd = eventfd,
            .events = POLLIN
            };
        this->pollfds.push_back(pfd);
      }
    }

    void reset_poll_fd() {
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

    struct pollfd* get_poll_fd() {
        return &this->pollfds[this->run_ctx_pollfd_idx.value()];
    }

  private:
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

      printf("Vfio-user: Bar region %d (offset 0x%x, size 0x%x) set up.\n", region->index, (uint)region->offset, (uint)region->size);

      return 0;
    }
};

int _main() {
  int ret;


  printf("hello 0x%X, %d, \n", VFIO_DEVICE_STATE_V1_RESUMING, VFIOC_SECRET);

  VfioUserServer vfu = VfioUserServer();

  // init vfio
  
  VfioConsumer vfioc;

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

  ret = vfu_pci_init(vfu.vfu_ctx, VFU_PCI_TYPE_EXPRESS, // TODO express
                     PCI_HEADER_TYPE_NORMAL, 0); // TODO 4?
  if (ret < 0) {
    die("vfu_pci_init() failed") ;
  }

  vfu_pci_set_id(vfu.vfu_ctx, 0x8086, 0x1592, 0x8086, 0x0002);
  int E810_REVISION = 0x02; // could be set by vfu_pci_set_class: vfu_ctx->pci.config_space->hdr.rid = 0x02;
  vfu_pci_set_class(vfu.vfu_ctx, 0x00, 0x00, 0x02);

  // set up vfio-user DMA

  ret = vfu.add_regions(vfioc.regions, vfioc.device);
  if (ret < 0)
    die("failed to add regions");

  // set up irqs 

  ret = vfu.add_irqs(vfioc.interrupts);
  if (ret < 0)
    die("failed to add irqs");

  vfioc.init_msix();
  vfu.add_msix_pollfds(vfioc.irqfds);

  // set capabilities
  Capabilities caps = Capabilities(&(vfioc.regions[VFU_PCI_DEV_CFG_REGION_IDX]), vfioc.mmio[VFU_PCI_DEV_CFG_REGION_IDX]);
  void *cap_data;
  size_t cap_size;
  cap_data = caps.msix(&cap_size);
  ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, VFU_CAP_FLAG_READONLY, cap_data);
  if (ret == 0)
    die("add cap error");
  
  // see lspci about which fields mean what https://github.com/pciutils/pciutils/blob/42e6a803bda392e98276b71994db0b0dd285cab1/ls-caps.c#L1469
  struct msixcap data;
  data.hdr = {
    .id = PCI_CAP_ID_MSIX,
  };
  data.mxc = {
    // message control
  };
  data.mtab = {
    // table offset / Table BIR
  };
  data.mpba = {
    // PBA offset / PBA BIR
  };
  //ret = vfu_pci_add_capability(vfu.vfu_ctx, 0, 0, &data);
  if (ret < 0) {
    die("Vfio-user: cannot add MSIX capability");
  }

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

  vfu.reset_poll_fd();

  // runtime loop
  
  // in our case this is msix (type 2) interrupts (0-1023)
  //ret = vfu_irq_trigger(vfu.vfu_ctx, 0xFFFFFFFF);

  do {
    ret = poll(vfu.pollfds.data(), vfu.pollfds.size(), 500);
    if (ret < 0) {
      die("failed to poll(2)");
    }

    if (vfu.get_poll_fd()->revents & (POLLIN)) {
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

    // check for MSIX interrupts to pass on
    for (uint64_t i = vfu.irq_pollfd_idx; i < vfu.irq_pollfd_idx + vfu.irq_pollfd_count; i++) {
      struct pollfd *pfd = &(vfu.pollfds[i]);
      if (pfd->revents & (POLLIN)) {
        // pass on (trigger) interrupt
        size_t irq_subindex = i - vfu.irq_pollfd_idx;
        ret = vfu_irq_trigger(vfu.vfu_ctx, irq_subindex);
        if (ret < 0) {
          die("Cannot trigger MSIX interrupt %lu", irq_subindex);
        }
        break;
      }
    }
  } while (!quit.load());

  // destruction is done by ~VfioUserServer

  return 0;
}

void signal_handler(int) {
  quit.store(true);
}

int main() {
  // register signal handler to handle SIGINT gracefully to call destructors
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);

  try {
    return _main();
  } catch (...) {
    // we seem to need this catch everyting so that our destructors work
    return EXIT_FAILURE;
  }

  return quit;
}

