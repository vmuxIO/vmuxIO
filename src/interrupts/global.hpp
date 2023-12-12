#pragma once

#include <memory>
#include <vector>

class InterruptThrottler;

class GlobalInterrupts {
private:
  std::vector<std::shared_ptr<InterruptThrottler>> throttlers;
  std::vector<std::atomic<ulong> *> spacings;

public:
  ulong spacing_max; // ns
  ulong spacing_avg; // ns, not actual mathematical average
  ulong spacing_min; // ns
  
  void add(std::shared_ptr<InterruptThrottler> throttler);
  void update();
};

