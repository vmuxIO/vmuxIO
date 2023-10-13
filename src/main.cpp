#include <atomic>
#include <memory>
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
#include <thread>

#include "device.hpp"
#include "src/vfio-consumer.hpp"
#include "src/util.hpp"
#include "src/caps.hpp"
#include <thread>
#include <src/libsimbricks/simbricks/nicbm/nicbm.h>
#include <src/sims/nic/e810_bm/e810_bm.h>

#include "src/vfio-server.hpp"
#include "src/util.hpp"
#include "src/runner.hpp"

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
[[maybe_unused]] static ssize_t bar0_access(
        vfu_ctx_t *vfu_ctx,
        char * const buf,
        size_t count,
        __loff_t offset,
        const bool is_write
        )
{
    vmux_dev_ctx_t *dev_ctx = (vmux_dev_ctx_t*)vfu_get_private(vfu_ctx);

    if (count > sizeof(dev_ctx->value)
            || offset + count > sizeof(dev_ctx->value)) {
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

    int ch;
    std::string device = "0000:18:00.0";
    std::vector<std::string> pciAddresses; 
    std::vector<std::unique_ptr<VmuxRunner>> runner;
    std::vector<std::shared_ptr<VfioConsumer>> vfioc;
    std::vector<std::shared_ptr<VmuxDevice>> devices;
    std::string group_arg;
    // int HARDWARE_REVISION; // could be set by vfu_pci_set_class:
                              // vfu_ctx->pci.config_space->hdr.rid = 0x02;
    std::vector<int> pci_ids;
    std::vector<std::string> sockets;
    std::vector<std::string> modes;
    std::vector<std::string> emulationDevices;
    while ((ch = getopt(argc,argv,"hd:s:m:")) != -1){
        switch(ch)
        {
            case 'd':
                pciAddresses.push_back(optarg);
                break;
            case 's':
                sockets.push_back(optarg);
                break;
            case 'm':
                modes.push_back(optarg);
                break;
            case 'e':
                emulationDevices.push_back(optarg);
                break;
            case '?':
            case 'h':
                std::cout <<
                    "-d 0000:18:00.0                        PCI-Device (or \"none\" if not applicable)\n" <<
                    "-s /tmp/vmux.sock                      Path of the socket\n" <<
                    "-m passthrough                         vMux mode: passthrough, emulation\n"
                    "-e e810                                emulation device: e810, e1000 (or \"none\")\n"
                    ;
                return 0;
            default:
                break;
        }
    }

    if (sockets.size() == 0)
        sockets.push_back("/tmp/vmux.sock");

    if (modes.size() == 0)
        modes.push_back("passthrough");

    if (sockets.size() != modes.size() || modes.size() != pciAddresses.size()) {
        errno = EINVAL;
        die("Command line arguments need to specify the same number of devices, sockets and modes");
    }

    // create vfio consumers
    for(size_t i = 0; i < pciAddresses.size(); i++) {
        if (pciAddresses[i] == "none") {
            vfioc.push_back(NULL);
            continue;
        }
        printf("Using: %s\n", pciAddresses[i].c_str());
        vfioc.push_back(std::make_shared<VfioConsumer>(pciAddresses[i].c_str()));

        if(vfioc[i]->init() < 0){
            die("failed to initialize vfio consumer");
        }
        if (vfioc[i]->init_mmio() < 0) {
            die("failed to initialize vfio mmio mappings");
        }
        vfioc[i]->init_legacy_irqs();
        vfioc[i]->init_msix();
    }

    // create devices
    for(size_t i = 0; i < pciAddresses.size(); i++) {
        std::shared_ptr<VmuxDevice> device;
        if (modes[i] == "passthrough") {
            device = std::make_shared<PassthroughDevice>(vfioc[i], pciAddresses[i]);
        }
        if (modes[i] == "stub") {
            device = std::make_shared<StubDevice>();
        }
        if (modes[i] == "emulation") {
            if (emulationDevices[i] == "e810") {
                device = std::make_shared<E810EmulatedDevice>();
            }
            if (emulationDevices[i] == "e1000") {
                device = std::make_shared<E1000EmulatedDevice>();
            }
        }
        devices.push_back(device);
    }

    int efd = epoll_create1(0);

    for(size_t i = 0; i < pciAddresses.size(); i++){
        printf("Using: %s\n", pciAddresses[i].c_str());
        runner.push_back(std::make_unique<VmuxRunner>(sockets[i], devices[i], efd));
        runner[i]->start();

        while(runner[i]->state !=2);
    }


    //VmuxRunner r(socket,device);
    //r.start();
    for(size_t i = 0; i < pciAddresses.size(); i++){
        while(!runner[i]->is_connected()){
            if(quit.load())
                break;
            usleep(10000);
        }
    }
    // printf("pfd->revents & POLLIN: %d\n",
    //        runner[0]->get_interrupts().pollfds[
    //            runner[0]->get_interrupts().irq_intx_pollfd_idx
    //            ].revents & POLLIN);
    
    // runtime loop
    while (!quit.load()) {
        for(size_t i = 0; i < runner.size(); i++){
            struct epoll_event events[1024];

            int eventsc = epoll_wait(efd, events, 1024,500);

            for(int i = 0; i < eventsc; i++){
                auto f = (interrupt_callback*)events[i].data.ptr;
                f->callback(f->fd,f->vfu);
            }
        }
    }

    for(size_t i = 0; i < pciAddresses.size(); i++){
        runner[i]->stop();
    }
    for(size_t i = 0; i < pciAddresses.size(); i++){
        runner[i]->join();
    }

    // destruction is done by ~VfioUserServer
    close(efd);
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
