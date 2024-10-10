#pragma once

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include "devices/e810.hpp"
#include "devices/vmux-device.hpp"
#include "util.hpp"

class PtpPolicy {
  const int schduling_interval = 20; // in seconds

  int timer_fd = 0;
  int efd = 0;
  epoll_callback timer_callback;

  std::shared_ptr<VmuxDevice> default_device;
  std::shared_ptr<std::vector<std::shared_ptr<VmuxDevice>>> broadcast_destinations;

public:
  PtpPolicy(std::shared_ptr<VmuxDevice> default_device, std::shared_ptr<std::vector<std::shared_ptr<VmuxDevice>>> broadcast_destinations, int efd): default_device(default_device), broadcast_destinations(broadcast_destinations) {
    // this->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    // struct timespec now;
    // if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
    //   die("Cant get time");
    // struct itimerspec its = {};
    // // its.it_value.tv_sec = now.tv_sec + this->schduling_interval;
    // // its.it_value.tv_nsec = now.tv_nsec;
    // its.it_value.tv_sec = this->schduling_interval;
    // its.it_value.tv_nsec = 0;
    // its.it_interval.tv_sec = this->schduling_interval;
    // its.it_interval.tv_nsec = 0;
    // timerfd_settime(this->timer_fd, 0, &its, NULL);

    int fd;
    struct itimerspec new_value;
    new_value.it_value.tv_sec = this->schduling_interval;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = this->schduling_interval;
    new_value.it_interval.tv_nsec = 0;

    fd = timerfd_create(CLOCK_MONOTONIC, 0);
     if (fd == -1)
         die("timerfd_create");

    if (timerfd_settime(fd, 0, &new_value, NULL) == -1)
         die("timerfd_settime");

    this->timer_fd = fd;
    this->registerEpoll(efd);
  }

  ~PtpPolicy() {
    close(this->timer_fd);
  }

  void registerEpoll(int efd) {
    this->timer_callback.fd = this->timer_fd;
    this->timer_callback.callback = PtpPolicy::timer_cb;
    this->timer_callback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->timer_callback;

    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, this->timer_fd, &e))
      die("could not register timer fd to epoll");

    this->efd = efd;
  }

  // every scheduling_interval seconds, tell the default device to direct PTP packets to another VM
  static void timer_cb(int fd, void* this__) {
    PtpPolicy* this_ = (PtpPolicy*) this__;
    auto e810dev = std::dynamic_pointer_cast<E810EmulatedDevice>(this_->default_device);
    if (e810dev) {
      auto vm_id = (e810dev->ptp_target_vm_idx.load() + 1) % this_->broadcast_destinations->size();
      e810dev->ptp_target_vm_idx.store(vm_id);
      printf("New PTP target: VM %lu\n", vm_id);
    }
    uint64_t nr_expirations = 0;
    read(this_->timer_fd, &nr_expirations, sizeof(uint64_t));
  }

};

