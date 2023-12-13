#pragma once

#include "devices/vmux-device.hpp"
#include "nic-emu.hpp"
#include "tap.hpp"
#include "util.hpp"
#include <bits/types/struct_itimerspec.h>
#include <cstring>
#include <ctime>
#include <sys/timerfd.h>
#include <time.h>
#include <cstdlib>
#include "interrupts/interface.hpp"

/*
 * Does many things, but the "physical" limit of e1000 of ~8000irq/s is enforced by behavioral model
 */
class InterruptThrottlerAccurate: public InterruptThrottler {
  public:
  struct timespec last_interrupt_ts = {};
  // ulong interrupt_spacing = 250 * 1000; // nsec
  std::atomic<bool> is_deferred = false;
  int timer_fd;
  int efd;
  int irq_idx;
  epoll_callback timer_callback;
  std::shared_ptr<VfioUserServer> vfuServer;
  ulong factor = 1;

  InterruptThrottlerAccurate(int efd, int irq_idx): irq_idx(irq_idx) {
    this->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0); // foo error
    this->registerEpoll(efd);

  }

  void registerEpoll(int efd) {
    this->timer_callback.fd = this->timer_fd;
    this->timer_callback.callback = InterruptThrottlerAccurate::timer_cb;
    this->timer_callback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->timer_callback;
    
    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, this->timer_fd, &e))
      die("could not register timer fd to epoll");

    this->efd = efd;
  }

  static void timer_cb(int fd, void* this__) {
    InterruptThrottlerAccurate* this_ = (InterruptThrottlerAccurate*) this__;
    this_->send_interrupt();
    struct itimerspec its = {};
    timerfd_settime(this_->timer_fd, 0, &its, NULL); // foo error
    this_->is_deferred.store(false); // TODO events can get lost which leads to deadlocks
  }
                            
  void defer_interrupt(int duration_ns) {
    // this->is_deferred.store(true);

    struct itimerspec its = {};
    its.it_value.tv_nsec = duration_ns;
    // its.it_interval.tv_nsec = 100 * 1000 * 1000;
    timerfd_settime(this->timer_fd, 0, &its, NULL); // foo error
  }

  /* interrupt_spacing in ns
   *
   * Qemu to my understanding defers interrupts by min(ITR, 111us) if an interrupt is still pending but last rx no interrupt was pending. It skips the interrupt, if another interrupt is already delayed. 
   *
   * We defer an interrupt if the last packet arrived longer than ITR us ago. Our BM drops interrupts, if interrupt is still raised (not masked)
   *
   * The e1000 should defer interrupts, if the last inerrupt was issued ITR us ago. 
   */

  __attribute__((noinline)) ulong try_interrupt(ulong interrupt_spacing, bool int_pending) {
    // struct itimerspec its = {};
    // timerfd_gettime(this->timer_fd, &its); // foo error
    // struct timespec* now = &its.it_value;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (Util::ts_before(&now, &this->last_interrupt_ts)) {
      return 1338;
    }
    ulong time_since_interrupt = Util::ts_diff(&now, &this->last_interrupt_ts);
    ulong defer_by = this->factor * Util::ulong_min(500000, Util::ulong_diff(interrupt_spacing, time_since_interrupt));
    int we_defer = 1337;
    bool if_false = false;
    // this->last_interrupt_ts = now;
    if (time_since_interrupt < interrupt_spacing || int_pending) {
      we_defer = this->is_deferred.compare_exchange_strong(if_false, true);
      if (we_defer) {
        // this->last_interrupt_ts.tv_nsec += defer_by;
        now.tv_nsec += defer_by; // estimate interrupt time now already. Otherwise we have to set it in the timer_cb which would require an additional lock.
        this->last_interrupt_ts = now;
        // ignore tv_nsec overflows. I think they will just lead to additional interrupts
        this->defer_interrupt(defer_by);
      }
    } else {
      this->last_interrupt_ts = now;
      this->send_interrupt();
    }

    return we_defer;
  }

  __attribute__((noinline)) void send_interrupt() {
    int ret = vfu_irq_trigger(this->vfuServer->vfu_ctx, this->irq_idx);
    if_log_level(LOG_DEBUG, printf("Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
    if (ret < 0) {
      die("Cannot trigger MSIX interrupt %d", this->irq_idx);
    }
  }
};
