#include <memory>
#include <algorithm>
#include "devices/e1000.hpp"
#include "interrupts/global.hpp"

GlobalInterrupts::GlobalInterrupts(int nr_threads) {
  this->timer.start();
}

void GlobalInterrupts::add(std::shared_ptr<InterruptThrottler> throttler) {
  this->throttlers.push_back(throttler);
  this->spacings.push_back(&(throttler->spacing));
}

void GlobalInterrupts::update() {
  // update timer statistics once a second
  this->cpu_time = this->timer.elapsed();
  if (this->cpu_time.wall > 1 * 1000 * 1000 * 1000) {
    this->cpu_usage =  (float)(this->cpu_time.user + this->cpu_time.system) / (this->cpu_time.wall * this->nr_threads);
    if (this->cpu_usage > 0.9) {
      this->slow_down *= 1.1;
    } else {
      this->slow_down = std::max(1.0, this->slow_down * 0.9);
    }
    this->timer.start();
  }

  // update interrupt spacing statistics
  for (auto spacing_ : this->spacings) {
    auto spacing = spacing_->load();
    if (spacing < this->spacing_min) {
      this->spacing_min = spacing;
    }
    if (spacing > this->spacing_max) {
      this->spacing_max = spacing;
    }
  }
  this->spacing_avg = (spacing_min + spacing_max) / 2;
}

