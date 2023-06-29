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

#include "src/vfio-server.hpp"
#include "src/util.hpp"
#include "src/runner.hpp"
extern "C" {
  #include "libvfio-user.h"
}



#include <atomic>


// set true by signals, should be respected by runtime loops
std::atomic<bool> quit(false); 

typedef struct {
  uint64_t value[2];
  void *bar1;
  size_t bar1size;
} vmux_dev_ctx_t;

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



int _main(int argc, char** argv) {
  int ret;

  int ch;
  std::string device = "0000:18:00.0";
  std::vector<std::string> devices; 
  std::vector<VmuxRunner*> runner;
  std::vector<VfioConsumer> vfioc;
  std::string group_arg;
  //int HARDWARE_REVISION; // could be set by vfu_pci_set_class: vfu_ctx->pci.config_space->hdr.rid = 0x02;
  std::vector<int> pci_ids;
  std::string socket = "/tmp/vmux.sock";
  while ((ch = getopt(argc,argv,"hd:s:")) != -1){
    switch(ch)
      {
      case 'd':
        devices.push_back(optarg);
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

  for(size_t i = 0; i < devices.size(); i++){
    printf("Using: %s\n", devices[i].c_str());
    vfioc.push_back(VfioConsumer(devices[i].c_str()));
    if(vfioc[i].init() < 0){
        die("failed to initialize vfio consumer");
    }
    if (vfioc[i].init_mmio() < 0) {
        die("failed to initialize vfio mmio mappings");
    }
    vfioc[i].init_legacy_irqs();
    vfioc[i].init_msix();
  }

 
  for(size_t i = 0; i < devices.size(); i++){
    printf("Using: %s\n", devices[i].c_str());
    runner.push_back(new VmuxRunner(std::string("/tmp/vmux_") + devices[i] + ".sock", devices[i], vfioc[i]));
    
    runner[i]->start();
     
    while(runner[i]->state !=2);
    
  }


  //VmuxRunner r(socket,device);
  //r.start();
  for(size_t i = 0; i < devices.size(); i++){
    while(!runner[i]->is_connected()){
      if(quit.load())
        break;
      usleep(10000);
    }
  }
  printf("pfd->revents & POLLIN: %d\n", runner[0]->get_interrupts().pollfds[runner[0]->get_interrupts().irq_intx_pollfd_idx].revents & POLLIN);
  
  // runtime loop
  while (!quit.load()) {
    for(size_t i = 0; i < runner.size(); i++){
      VfioUserServer& vfu = runner[i]->get_interrupts();
      ret = poll(vfu.pollfds.data(), vfu.pollfds.size(), 500);
      printf("%zu %zu %zu %d\n", vfu.pollfds.size(),vfu.irq_intx_pollfd_idx, vfu.irq_msi_pollfd_idx,ret);
      if (ret < 0) {
        die("failed to poll(2)");
      }
      printf("pfd->revents & POLLIN: %d\n", runner[0]->get_interrupts().pollfds[runner[0]->get_interrupts().irq_intx_pollfd_idx].revents & POLLIN);
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
          printf("Triggered interrupt. ret = %d, errno: %d\n", ret,errno);
          if (ret < 0) {
            die("Cannot trigger MSIX interrupt %lu", irq_subindex);
          }
          break;
        }
      }
    }
  }

  for(size_t i = 0; i < devices.size(); i++){
    runner[i]->stop();
  }
  for(size_t i = 0; i < devices.size(); i++){
    runner[i]->join();
  }
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

