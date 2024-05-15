#include "sims/nic/e810_bm/e810_ptp.h"
#include <cstdint>
#include <stdlib.h>
#include <string.h>

#include <cassert>
#include <iostream>

#include "sims/nic/e810_bm/e810_base_wrapper.h"
#include "sims/nic/e810_bm/e810_bm.h"


namespace i40e {

ptpmgr::ptpmgr(e810_bm &dev_)
  : dev(dev_), last_updated(0), last_val(0), offset(0), inc_val(0),
    adj_neg(false), adj_val(0) {
}

gltsyn_ts_t ptpmgr::update_clock() {
  /* this simulates the behavior of the PHC, but instead of doing it cycle by
     cycle, we calculate updates when the clock is accessed or parameters are
     modified, applying the same changes that should have happened cycle by
     cycle. Before modifying any of the parameters update_clock has to be
     called to get the correct behavior, to ensure e.g. that updates to adj and
     inc are applied at the correct points in time.*/
  uint64_t ps_per_cycle = 1000000000000ULL / CLOCK_HZ;
  uint64_t cycle_now = 1; // dev.runner_->TimePs() / ps_per_cycle;
  uint64_t cycles_passed = cycle_now - last_updated;

  // increment clock
  last_val += (__uint128_t) inc_val * cycles_passed;

  // factor in adjustments
  if (adj_val != 0) {
    __uint128_t adj;
    if (adj_val <= cycles_passed) {
      adj = cycles_passed;
      adj_val -= cycles_passed;
    } else {
      adj = adj_val;
      adj_val = 0;
    }

    adj = adj << 32;
    if (adj_neg)
      last_val -= adj;
    else
      last_val += adj;
  }

  last_updated = cycle_now;

  gltsyn_ts_t next_val = {{ (last_val.value >> (__int128) 32) + offset.value }};
  return next_val;
}

gltsyn_ts_t ptpmgr::phc_read() {
  return update_clock();
}

void ptpmgr::phc_write(gltsyn_ts_t val) {
  gltsyn_ts_t cur_val = update_clock();
  offset.value += (val.value - cur_val.value);
}

uint64_t ptpmgr::adj_get() {
  update_clock();
  return adj_val;
}

void ptpmgr::adj_set(uint64_t val) {
  update_clock();
  adj_val = val;
}

void ptpmgr::inc_set(uint64_t inc) {
  update_clock();
  inc_val = inc;
}

}
