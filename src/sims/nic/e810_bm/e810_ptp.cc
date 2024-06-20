#include <cstdint>
#include <stdlib.h>
#include <string.h>

#include <cassert>
#include <iostream>

#include "sims/nic/e810_bm/e810_ptp.h"
#include "sims/nic/e810_bm/e810_bm.h"


namespace e810 {

ptpmgr::ptpmgr(e810_bm &dev_)
  : dev(dev_), last_updated(0), last_val({ .value=0 }), offset({ .value=0 }), inc_val( 0x100000000 ),
    adj_neg(false), adj_val(0) {
}

e810_timestamp_t ptpmgr::update_clock() {

  uint64_t ps_per_cycle = 1000000000000ULL / CLOCK_HZ;
  uint64_t cycle_now = dev.ReadCurrentTimestamp().time / ps_per_cycle;
  uint64_t cycles_passed = last_updated - cycle_now;

  // increment clock
  last_val.value += (__uint128_t) inc_val * cycles_passed;

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
      last_val.value -= (__int128)adj;
    else
      last_val.value += (__int128)adj;
  }

  last_updated = cycle_now;

  e810_timestamp_t next_val = { .value = (last_val.value + ((__int128) offset.value << 32)) };
  return last_val;
}

e810_timestamp_t ptpmgr::phc_read() {
  return update_clock();
}

void ptpmgr::phc_write(e810_timestamp_t val) {
  e810_timestamp_t cur_val = update_clock();
  offset.value += (val.value - cur_val.value);
}

uint64_t ptpmgr::adj_get() {
  update_clock();
  return adj_val;
}

uint64_t ptpmgr::inc_get() {
  update_clock();
  return inc_val;
}

void ptpmgr::adj_set(uint64_t val) {
  update_clock();
  adj_val = val;
}

void ptpmgr::inc_set(uint64_t inc) {
  update_clock();

  std::cout << "SET INCVAL" << inc << std::endl;
  inc_val = inc;
}

}
