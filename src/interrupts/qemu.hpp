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
class InterruptThrottlerQemu: public InterruptThrottler {
  private:
  struct timespec last_interrupt_ts = {};
  // ulong interrupt_spacing = 250 * 1000; // nsec
  std::atomic<bool> is_deferred = false;
  int timer_fd;
  int efd;
  int irq_idx;
  epoll_callback timer_callback;
  ulong factor = 10; // 5: good latency and ok mpps, but 3k irq/s. 10: perfect irq/s, Good throughput and bad latency.
  bool mit_irq_level = false;
  bool irq_level = false;

  public: 
  InterruptThrottlerQemu(int efd, int irq_idx, std::shared_ptr<GlobalInterrupts> irq_glob): irq_idx(irq_idx) {
    this->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0); // foo error
    this->registerEpoll(efd);
    this->globalIrq = irq_glob;

  }

  private:
  void registerEpoll(int efd) {
    this->timer_callback.fd = this->timer_fd;
    this->timer_callback.callback = InterruptThrottlerQemu::timer_cb;
    this->timer_callback.ctx = this;
    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.ptr = &this->timer_callback;
    
    if (0 != epoll_ctl(efd, EPOLL_CTL_ADD, this->timer_fd, &e))
      die("could not register timer fd to epoll");

    this->efd = efd;
  }

  static void timer_cb(int fd, void* this__) {
    InterruptThrottlerQemu* this_ = (InterruptThrottlerQemu*) this__;
    this_->send_interrupt();
    struct itimerspec its = {};
    timerfd_settime(this_->timer_fd, 0, &its, NULL); // foo error
    this_->is_deferred.store(false); // TODO events can get lost which leads to deadlocks
  }
                            
  __attribute__((noinline)) void defer_interrupt(int duration_ns) {
    // this->is_deferred.store(true);

    struct itimerspec its = {};
    its.it_value.tv_nsec = duration_ns;
    // its.it_interval.tv_nsec = 100 * 1000 * 1000;
    timerfd_settime(this->timer_fd, 0, &its, NULL); // foo error
  }

  public: 
  /* interrupt_spacing in ns
   *
   * Qemu to my understanding defers interrupts by min(ITR, 111us) if an interrupt is still pending but last rx no interrupt was pending. It skips the interrupt, if another interrupt is already delayed. 
   *
   * We defer an interrupt if the last packet arrived longer than ITR us ago. Our BM drops interrupts, if interrupt is still raised (not masked)
   *
   * The e1000 should defer interrupts, if the last inerrupt was issued ITR us ago. 
   */

  __attribute__((noinline)) ulong try_interrupt(ulong interrupt_spacing, bool no_int_pending) {
    this->spacing = interrupt_spacing;
    this->globalIrq->update();
    // struct itimerspec its = {};
    // timerfd_gettime(this->timer_fd, &its); // foo error
    // struct timespec* now = &its.it_value;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint pending_ints = !no_int_pending;

    // ulong time_since_interrupt = Util::ts_diff(&now, &this->last_interrupt_ts);
    // ulong defer_by = this->factor * Util::ulong_min(500000, Util::ulong_diff(interrupt_spacing, time_since_interrupt));
    // ulong defer_by = Util::ulong_max(this->globalIrq->spacing_avg, interrupt_spacing);
    int mit_timer_off = 1337;
    bool if_false = false;
    // this->last_interrupt_ts = now;
  
    if (!this->mit_irq_level && pending_ints) {
      // return if mit_timer_on
      mit_timer_off = this->is_deferred.compare_exchange_strong(if_false, true);
      if (!mit_timer_off) {
        return mit_timer_off;
      }

      // skipped check here: if MIT flag on

      ulong mit_delay = interrupt_spacing; 
      mit_delay = this->factor * Util::ulong_max(mit_delay, 128000); // spacing must not be shorter than 7813 irq/s => 128000ns.
      
      // we already set mit_timer_on via is_deferred.exchange()
      this->defer_interrupt(mit_delay); 
    }

    this->mit_irq_level = pending_ints != 0;
    this->pci_set_irq(this->mit_irq_level);

    return mit_timer_off;
  }

  private:
  __attribute__((noinline)) void pci_set_irq(bool level) {
    if (this->irq_level && !level) {
      // lower interrupt
      this->irq_level = false;
    } else if (!this->irq_level && level) {
      // raise interrupt
      this->irq_level = true;
      this->send_interrupt();
    }
  }

  __attribute__((noinline)) void send_interrupt() {
    int ret = vfu_irq_trigger(this->vfuServer->vfu_ctx, this->irq_idx);
    if_log_level(LOG_DEBUG, printf("Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
    if (ret < 0) {
      die("Cannot trigger MSIX interrupt %d", this->irq_idx);
    }
  }
};
