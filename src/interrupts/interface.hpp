#pragma once
#include <memory>
#include "vfio-server.hpp"
#include "interrupts/global.hpp"

class InterruptThrottler {
  public:
  std::shared_ptr<VfioUserServer> vfuServer;
  std::shared_ptr<GlobalInterrupts> globalIrq;
  std::atomic<ulong> spacing = 0; // us

  // InterruptThrottler(int efd, int irq_idx) {};
  virtual ulong try_interrupt(ulong interrupt_spacing, bool int_pending) = 0;
  virtual ~InterruptThrottler() = default;
};

