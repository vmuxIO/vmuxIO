#include <memory>
#include "devices/e1000.hpp"

void GlobalInterrupts::add(std::shared_ptr<InterruptThrottler> throttler) {
  this->throttlers.push_back(throttler);
  this->spacings.push_back(&(throttler->spacing));
}

void GlobalInterrupts::update() {
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

