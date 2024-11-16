#include <atomic>
#include <cstdlib>
#include <dirent.h>
#include <err.h>
#include <errno.h>
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
#include <boost/outcome.hpp>

#include "devices/vdpdk.hpp"
#include "src/caps.hpp"
#include "src/util.hpp"
#include "src/vfio-consumer.hpp"
#include "src/vfio-server.hpp"

#include "src/runner.hpp"

#ifdef BUILD_E1000_EMU
  #include "devices/e1000.hpp"
#endif
#include "devices/e810.hpp"
#include "devices/passthrough.hpp"
#include "src/devices/vmux-device.hpp"
#include "src/drivers/dpdk.hpp"
#include "src/drivers/tap.hpp"
#include "src/rx-thread.hpp"

extern "C" {
#include "libvfio-user.h"
}

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

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

Result<int> foo() {
  return Err("foo failed");
}

Result<void> _main(int argc, char **argv) {
  // auto i = expect(foo(), "aborted vmux1");

  int ch;
  std::string base_mac_str = "52:54:00:fa:00:60";
  std::string device = "0000:18:00.0";
  std::vector<std::string> pciAddresses;
  std::shared_ptr<GlobalInterrupts> globalIrq;
  std::vector<std::unique_ptr<VmuxRunner>> runner;
  std::vector<std::shared_ptr<VfioConsumer>> vfioc;
  std::vector<std::shared_ptr<VmuxDevice>> devices; // all devices
  std::vector<std::shared_ptr<VmuxDevice>>
      mainThreadPolling; // devices backed by poll based drivers
  std::vector<std::shared_ptr<VfioUserServer>> vfuServers;
  std::vector<std::shared_ptr<Driver>> drivers; // network backend for emulation
  std::vector<std::unique_ptr<RxThread>> pollingThreads;
  std::string group_arg;
  // int HARDWARE_REVISION; // could be set by vfu_pci_set_class:
  // vfu_ctx->pci.config_space->hdr.rid = 0x02;
  std::vector<int> pci_ids;
  std::vector<std::string> tapNames;
  std::vector<std::string> sockets;
  std::vector<std::string> modes;
  std::vector<cpu_set_t> rxThreadCpus;
  std::vector<cpu_set_t> runnerThreadCpus;
  cpu_set_t default_cpuset;
  Util::parse_cpuset("0-6", default_cpuset);
  bool useDpdk = false;
  bool pollInMainThread = false;
  uint8_t mac_addr[6];
  cpu_set_t cpuset;
  while ((ch = getopt(argc, argv, "hd:t:s:m:a:e:f:b:qu")) != -1) {
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
    case 'e':
      if (!Util::parse_cpuset(optarg, cpuset)) {
        die("vmuxRx%zu, Cannot parse cpu pinning set\n", rxThreadCpus.size())
      }
      rxThreadCpus.push_back(cpuset);
      break;
    case 'f':
      if (!Util::parse_cpuset(optarg, cpuset)) {
        die("vmuxRUnner%zu, Cannot parse cpu pinning set\n", runnerThreadCpus.size())
      }
      runnerThreadCpus.push_back(cpuset);
      break;
    case '?':
    case 'h':
      std::cout
          << argv[0] << " [VMUX-OPTIONS] [-- DPDK-OPTIONS]\n"
          << "-q                                     Quiet: reduce log level\n"
          << "-b " << base_mac_str
          << "                   Start assigning MAC "
             "address to emulated devices starting from this base\n"
          << "-u                                     Use dpdk backend instead "
             "of linux taps\n"
          << "-d 0000:18:00.0                        PCI-Device (or "
             "\"none\" if not applicable)\n"
          << "-t tap-username0                       Tap device to use "
             "as backend for emulation (or \"none\" if not applicable)\n"
          << "-s /tmp/vmux.sock                      Path of the socket\n"
          << "-m passthrough                         vMux mode: "
             "passthrough, emulation, mediation, e1000-emu\n"
          << "-e cpuset                              pin Rx thread to cpus. Takes arguements similar to cpuset. Default: 0-6\n"
          << "-f cpuset                              pin Runner thread to cpus.\n";
      return outcome::success();
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

  // fill default cpusets
  for (size_t i = rxThreadCpus.size();  i <= sockets.size(); i++) {
    rxThreadCpus.push_back(default_cpuset);
  }
  for (size_t i = runnerThreadCpus.size();  i <= sockets.size(); i++) {
    runnerThreadCpus.push_back(default_cpuset);
  }

  // parse base mac
  uint8_t base_mac[6];
  int ret = Util::str_to_mac(base_mac_str.c_str(), &base_mac);
  if (ret) {
    errno = EINVAL;
    die("Could not parse base MAC address (%d)", ret);
  }

  // start setting up vmux

  Util::check_clock_accuracy();

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
    char **dpdk_argv = &(argv[optind - 1]); // first arg is at index 0: "--"
    size_t dpdk_argc;
    if (optind < argc) {
      // -- arg used
      dpdk_argc = argc - optind + 1; // account for first arg
    } else {
      // no -- arg
      dpdk_argc = 0;
    }

    auto dpdk =
        std::make_shared<Dpdk>(sockets.size(), &base_mac, dpdk_argc, dpdk_argv);
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

    // increment base_mac
    memcpy(mac_addr, base_mac, sizeof(mac_addr));
    Util::intcrement_mac(mac_addr, i);

    // create device
    if (modes[i] == "passthrough") {
      device = std::make_shared<PassthroughDevice>(i, vfioc[i], pciAddresses[i]);
    }
    if (modes[i] == "stub") {
      device = std::make_shared<StubDevice>();
    }
    if (modes[i] == "emulation") {
      device = std::make_shared<E810EmulatedDevice>(i, drivers[i], efd, &mac_addr, globalIrq);
    }
    if (modes[i] == "mediation") {
      device = std::make_shared<E810EmulatedDevice>(i, drivers[i], efd, &mac_addr, globalIrq);
      device->driver->mediation_enable(i);
    }
    if (modes[i] == "vdpdk") {
      device = std::make_shared<VdpdkDevice>(i, drivers[i]);
    }
    if (modes[i] == "e1000-emu") {
#ifdef BUILD_E1000_EMU
      device = std::make_shared<E1000EmulatedDevice>(i, drivers[i], efd, true,
                                                     globalIrq, &mac_addr);
#else
      die("E1000 emulation support was disabled for this build.");
#endif
    }
    if (device == NULL)
      die("Unknown mode specified: %s\n", modes[i].c_str());
    devices.push_back(device);
    if (useDpdk && pollInMainThread)
      mainThreadPolling.push_back(device);
    if (useDpdk && !pollInMainThread) {
      pollingThreads.push_back(std::make_unique<RxThread>(device, rxThreadCpus[i]));
    }
  }

  for (size_t i = 0; i < pciAddresses.size(); i++) {
    vfuServers.push_back(
        std::make_shared<VfioUserServer>(sockets[i], efd, devices[i]));
  }

  for (size_t i = 0; i < pciAddresses.size(); i++) {
    printf("Using: %s\n", pciAddresses[i].c_str());
    runner.push_back(std::make_unique<VmuxRunner>(sockets[i], devices[i], efd,
                                                  vfuServers[i], runnerThreadCpus[i]));
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

  for (auto &pollingThread : pollingThreads) {
    pollingThread->start();
  }

  // printf("pfd->revents & POLLIN: %d\n",
  //        runner[0]->get_interrupts().pollfds[
  //            runner[0]->get_interrupts().irq_intx_pollfd_idx
  //            ].revents & POLLIN);

  // runtime loop
  int poll_timeout;
  if (useDpdk && pollInMainThread) {
    poll_timeout = 0; // dpdk: busy polling
  } else {
    poll_timeout = 500; // default: event based
  }
  bool foobar = false;
  while (!quit.load()) {
    for (size_t i = 0; i < runner.size(); i++) {
      struct epoll_event events[1024];

#ifdef BUILD_E1000_EMU
      if (foobar) {
        // simulate that the NIC received a small bogus packet
        uint64_t data = 0xdeadbeef;
        std::dynamic_pointer_cast<E1000EmulatedDevice>(devices[0])
            ->ethRx((char *)&data, sizeof(data));
        foobar = false;
      }
#endif
      for (size_t j = 0; j < mainThreadPolling.size(); j++) {
        // dpdk: do busy polling
        devices[j]->rx_callback(j, devices[j].get());
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
    pollingThreads[i]->stop();
  }

  Result<void> res = Ok();
  for (size_t i = 0; i < pciAddresses.size(); i++) {
    if (Result<void> e = runner[i]->join()) {} else {
      printf("Runner thread %zu failed: %s\n", i, e.error().c_str());
      res = Err("Terminating because a thread failed.");
    }
    if (Result<void> e = pollingThreads[i]->join()) {} else {
      printf("Poling rx thread %zu failed: %s\n", i, e.error().c_str());
      res = Err("Terminating because a thread failed.");
    }
  }

  // destruction is done by ~VfioUserServer
  close(efd);
  return res;
}

void signal_handler(int) { quit.store(true); }

int main(int argc, char **argv) {
  // register signal handler to handle signals gracefully to call destructors
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigfillset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  if (outcome::result<void, std::string> res = _main(argc, argv)) {
    return EXIT_SUCCESS;
  } else {
    auto e = res.error();
    printf("%s\n", e.c_str());
    return EXIT_FAILURE;
  }
}
