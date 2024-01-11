#pragma once
#include "interrupts/interface.hpp"
#include "vfio-server.hpp"

class InterruptThrottlerNone : public InterruptThrottler {
  private:
  int irq_idx;

  public: 
  InterruptThrottlerNone(int efd, int irq_idx, std::shared_ptr<GlobalInterrupts> irq_glob): irq_idx(irq_idx) {}

  ulong try_interrupt(ulong interrupt_spacing, bool no_int_pending) {
    this->send_interrupt();
    return true;
  }

  __attribute__((noinline)) void send_interrupt() {
    int ret = vfu_irq_trigger(this->vfuServer->vfu_ctx, this->irq_idx);
    if_log_level(LOG_DEBUG, printf("Triggered interrupt. ret = %d, errno: %d\n", ret, errno));
    if (ret < 0) {
      die("Cannot trigger MSIX interrupt %d", this->irq_idx);
    }
  }
};
