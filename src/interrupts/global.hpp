#pragma once

#include <memory>
#include <vector>
#include <boost/timer/timer.hpp>

class InterruptThrottler;

class GlobalInterrupts {
private:
  std::vector<std::shared_ptr<InterruptThrottler>> throttlers;
  std::vector<std::atomic<ulong> *> spacings;
  boost::timer::cpu_timer timer;
  boost::timer::cpu_times cpu_time;
  int nr_threads;

public:
  ulong spacing_max; // ns
  ulong spacing_avg; // ns, not actual mathematical average
  ulong spacing_min; // ns
  float cpu_usage = 0; // [0, 1]
  float slow_down = 1; // slow down interrupt rates due to cpu when > 1
  
  GlobalInterrupts(int nr_threads);
  void add(std::shared_ptr<InterruptThrottler> throttler);
  void update();
};

