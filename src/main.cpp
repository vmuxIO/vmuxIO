#include <atomic>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <signal.h>
#include <src/libsimbricks/simbricks/nicbm/nicbm.h>
#include <src/sims/nic/e810_bm/e810_bm.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "src/caps.hpp"
#include "src/util.hpp"
#include "src/vfio-consumer.hpp"
#include "src/vfio-server.hpp"

#include "src/runner.hpp"

#include "devices/e1000.hpp"
#include "devices/e810.hpp"
#include "devices/passthrough.hpp"
#include "src/devices/vmux-device.hpp"
#include "src/drivers/tap.hpp"
#include "src/drivers/dpdk.hpp"

extern "C" {
#include "libvfio-user.h"
}

// set true by signals, should be respected by runtime loops
std::atomic<bool> quit(false);

typedef struct {
  uint64_t value[2];
  void *bar1;
  size_t bar1size;
} vmux_dev_ctx_t;

// keep as reference for now, how bar callback functions should work
[[maybe_unused]] static ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf,
                                            size_t count, __loff_t offset,
                                            const bool is_write) {
  vmux_dev_ctx_t *dev_ctx = (vmux_dev_ctx_t *)vfu_get_private(vfu_ctx);

  if (count > sizeof(dev_ctx->value) ||
      offset + count > sizeof(dev_ctx->value)) {
    vfu_log(vfu_ctx, LOG_ERR, "bad BAR0 access %#llx-%#llx",
            (unsigned long long)offset, (unsigned long long)offset + count - 1);
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

int _main(int argc, char **argv) {
  int ch;
  std::string base_mac_str = "52:54:00:fa:00:60";
  std::string device = "0000:18:00.0";
  std::vector<std::string> pciAddresses;
  std::shared_ptr<GlobalInterrupts> globalIrq;
  std::vector<std::unique_ptr<VmuxRunner>> runner;
  std::vector<std::shared_ptr<VfioConsumer>> vfioc;
  std::vector<std::shared_ptr<VmuxDevice>> devices;
  std::vector<std::shared_ptr<VfioUserServer>> vfuServers;
  std::vector<std::shared_ptr<Driver>> drivers; // network backend for emulation
  std::string group_arg;
  // int HARDWARE_REVISION; // could be set by vfu_pci_set_class:
  // vfu_ctx->pci.config_space->hdr.rid = 0x02;
  std::vector<int> pci_ids;
  std::vector<std::string> tapNames;
  std::vector<std::string> sockets;
  std::vector<std::string> modes;
  bool useDpdk = false;
  while ((ch = getopt(argc, argv, "hd:t:s:m:b:qu")) != -1) {
    switch (ch) {
    case 'q':
      LOG_LEVEL = LOG_ERR;
      break;
    case 'b':
      base_mac_str = optarg;
      break;
    case 'u':
      useDpdk = true;
      break;
    case 'd':
      pciAddresses.push_back(optarg);
      break;
    case 't':
      tapNames.push_back(optarg);
      break;
    case 's':
      sockets.push_back(optarg);
      break;
    case 'm':
      modes.push_back(optarg);
      break;
    case '?':
    case 'h':
      std::cout << argv[0] << " [VMUX-OPTIONS] [-- DPDK-OPTIONS]\n"
          << "-q                                     Quiet: reduce log level\n"
          << "-b " << base_mac_str << "                   Start assigning MAC "
             "address to emulated devices starting from this base\n"
          << "-u                                     Use dpdk backend instead of linux taps\n"
          << "-d 0000:18:00.0                        PCI-Device (or "
             "\"none\" if not applicable)\n"
          << "-t tap-username0                       Tap device to use "
             "as backend for emulation (or \"none\" if not applicable)\n"
          << "-s /tmp/vmux.sock                      Path of the socket\n"
          << "-m passthrough                         vMux mode: "
             "passthrough, emulation, e1000-emu\n";
      return 0;
    default:
      break;
    }
  }

  if (sockets.size() == 0)
    sockets.push_back("/tmp/vmux.sock");

  if (modes.size() == 0)
    modes.push_back("passthrough");

  if (tapNames.size() == 0)
    tapNames.push_back("none");

  if (sockets.size() != modes.size() || modes.size() != pciAddresses.size()) {
    errno = EINVAL;
    die("Command line arguments need to specify the same number of devices, "
        "sockets and modes");
  }
  if (!useDpdk && pciAddresses.size() != tapNames.size()) {
    errno = EINVAL;
    die("Command line arguments need to specify the same number of devices, "
        "taps, sockets and modes");
  }
  
  // start setting up vmux

  int efd = epoll_create1(0);

  if (!useDpdk) {
    // create taps
    for (size_t i = 0; i < tapNames.size(); i++) {
      if (tapNames[i] == "none") {
        drivers.push_back(NULL);
        continue;
      }
      auto tap = std::make_shared<Tap>();
      tap->open_tap(tapNames[i].c_str());
      drivers.push_back(tap);
    }
  } else {
    // init dpdk
    // move to after vfu sock creation
    char** dpdk_argv = &(argv[optind-1]); // first arg is at index 0: "--"
    size_t dpdk_argc;
    if (optind < argc) {
      // -- arg used
      dpdk_argc = argc - optind + 1; // account for first arg
    } else {
      // no -- arg
      dpdk_argc = 0;
    }

    auto dpdk = std::make_shared<Dpdk>(dpdk_argc, dpdk_argv);
    for (size_t i = 0; i < sockets.size(); i++) {
      drivers.push_back(dpdk); // everyone shares a single dpdk backend
    }
  }

  // create vfio consumers
  for (size_t i = 0; i < pciAddresses.size(); i++) {
    if (pciAddresses[i] == "none") {
      vfioc.push_back(NULL);
      continue;
    }
    printf("Using: %s\n", pciAddresses[i].c_str());
    vfioc.push_back(std::make_shared<VfioConsumer>(pciAddresses[i].c_str()));

    if (vfioc[i]->init() < 0) {
      die("failed to initialize vfio consumer");
    }
    if (vfioc[i]->init_mmio() < 0) {
      die("failed to initialize vfio mmio mappings");
    }
    vfioc[i]->init_legacy_irqs();
    vfioc[i]->init_msix();
  }

  int nr_threads = vfioc.size() + 1; // runner threads + 1 main thread
  globalIrq = std::make_shared<GlobalInterrupts>(nr_threads);

  // create devices
  for (size_t i = 0; i < pciAddresses.size(); i++) {
    std::shared_ptr<VmuxDevice> device = NULL;
    if (modes[i] == "passthrough") {
      device = std::make_shared<PassthroughDevice>(vfioc[i], pciAddresses[i]);
    }
    if (modes[i] == "stub") {
      device = std::make_shared<StubDevice>();
    }
    if (modes[i] == "emulation") {
      device = std::make_shared<E810EmulatedDevice>();
    }
    if (modes[i] == "e1000-emu") {
      // parse base mac
      uint8_t base_mac[6];
      int ret = Util::str_to_mac(base_mac_str.c_str(), &base_mac);
      if (ret) {
        die("Could not parse base MAC address (%d)", ret);
      }
      
      // increment base_mac
      uint8_t mac_addr[6];
      memcpy(mac_addr, base_mac, sizeof(mac_addr));
      Util::intcrement_mac(mac_addr, i);

      // create device
      device = std::make_shared<E1000EmulatedDevice>(drivers[i], efd, true, globalIrq, &mac_addr);
    }
    if (device == NULL)
      die("Unknown mode specified: %s\n", modes[i].c_str());
    devices.push_back(device);
  }

  for (size_t i = 0; i < pciAddresses.size(); i++) {
    vfuServers.push_back(
        std::make_shared<VfioUserServer>(sockets[i], efd, devices[i]));
  }

  for (size_t i = 0; i < pciAddresses.size(); i++) {
    printf("Using: %s\n", pciAddresses[i].c_str());
    runner.push_back(std::make_unique<VmuxRunner>(sockets[i], devices[i], efd,
                                                  vfuServers[i]));
    runner[i]->start();

    while (runner[i]->state != 2)
      ;
  }

  // VmuxRunner r(socket,device);
  // r.start();
  for (size_t i = 0; i < pciAddresses.size(); i++) {
    while (!runner[i]->is_connected()) {
      if (quit.load())
        break;
      usleep(10000);
    }
  }
  // printf("pfd->revents & POLLIN: %d\n",
  //        runner[0]->get_interrupts().pollfds[
  //            runner[0]->get_interrupts().irq_intx_pollfd_idx
  //            ].revents & POLLIN);

  // runtime loop
  int poll_timeout;
  if (useDpdk) {
    poll_timeout = 0; // dpdk: busy polling
  } else {
    poll_timeout = 500; // default: event based
  }
  bool foobar = false;
  while (!quit.load()) {
    for (size_t i = 0; i < runner.size(); i++) {
      struct epoll_event events[1024];

      if (foobar) {
        // simulate that the NIC received a small bogus packet
        uint64_t data = 0xdeadbeef;
        std::dynamic_pointer_cast<E1000EmulatedDevice>(devices[0])
            ->ethRx((char *)&data, sizeof(data));
        foobar = false;
      }
      if (useDpdk) {
        E1000EmulatedDevice::driver_cb(0, devices[0].get()); // TODO multi-VM support
        // drivers[0]->recv(); // dpdk: do busy polling // TODO support multiple VMs
      }
      int eventsc = epoll_wait(efd, events, 1024, poll_timeout);
      // printf("poll main %d\n", eventsc);

      for (int i = 0; i < eventsc; i++) {
        auto f = (epoll_callback *)events[i].data.ptr;
        f->callback(f->fd, f->ctx);
      }
    }
  }

  for (size_t i = 0; i < pciAddresses.size(); i++) {
    runner[i]->stop();
  }
  for (size_t i = 0; i < pciAddresses.size(); i++) {
    runner[i]->join();
  }

  // destruction is done by ~VfioUserServer
  close(efd);
  return 0;
}

void signal_handler(int) { quit.store(true); }

int main(int argc, char **argv) {
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
