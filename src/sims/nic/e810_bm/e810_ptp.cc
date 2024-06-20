#include <cstdint>
#include <stdlib.h>
#include <string.h>

#include <cassert>
#include <iostream>

#include "sims/nic/e810_bm/e810_ptp.h"
#include "sims/nic/e810_bm/e810_bm.h"
#include "devices/e810.hpp"

namespace e810 {

ptpmgr::ptpmgr(e810_bm &dev_)
  : dev(dev_), last_updated(0), last_val({ .value=0 }), offset({ .value = 0 }), inc_val( 0x100000000 ) {
}


e810_timestamp_t ptpmgr::phc_read() {
  // Mediation: if device is emulated, use hw timestamp
  if (auto e810_dev = dynamic_pointer_cast<E810EmulatedDevice>(this->dev.vmux->device)) {
    struct timespec ts = e810_dev->driver->readCurrentTimestamp();
    this->last_val = { .time=TIMESPEC_TO_NANOS(ts), .time_0=0 };
    

  // Emulation: use current unix time
  } else {
    struct timespec ts;
    if(!clock_gettime(CLOCK_MONOTONIC, &ts)) {
      printf("Error: Could not get unix timestamp. \n");
      return { .value=0 };

    } else {

      /* when emulating, we use an internal timer based on the deltas between time stamps 
         and the current INC_VAL */
      constexpr uint64_t ps_per_cycle = 1'000'000'000'000ULL / CLOCK_HZ;

      uint64_t cycle_now = TIMESPEC_TO_NANOS(ts) / ps_per_cycle;
      uint64_t cycles_passed = last_updated - cycle_now;

      this->last_updated = cycle_now;
      this->last_val.value += (__int128) this->inc_val * cycles_passed;
    } 
  }

  // factor in adjustments
  e810_timestamp_t adjusted_timestamp;

  // TODO: Offset should depend on guest to allow multi vm timestamping
  if (this->offset.value != 0) {
      last_val.value += (__int128) (offset.value & E810_TIMESTAMP_MASK);
  }

  return this->last_val;
}
  
e810_timestamp_t ptpmgr::phc_sample_rx(uint16_t portid) {

  if (auto e810_dev = dynamic_pointer_cast<E810EmulatedDevice>(this->dev.vmux->device)) {
    struct timespec timespec = e810_dev->driver->readRxTimestamp(portid);
    return { .time=TIMESPEC_TO_NANOS(timespec), .time_0=0 };

  } else {
    return this->phc_read();
  }
}

e810_timestamp_t ptpmgr::phc_sample_tx(uint16_t portid) {
  if (auto e810_dev = dynamic_pointer_cast<E810EmulatedDevice>(this->dev.vmux->device)) {
    struct timespec timespec = e810_dev->driver->readTxTimestamp(portid);
    return { .time=TIMESPEC_TO_NANOS(timespec), .time_0=0 };

  } else {
    return this->phc_read();
  }
}


void ptpmgr::phc_write(e810_timestamp_t val) {
  this->offset.value += (val.value - this->last_val.value);
}

uint64_t ptpmgr::get_incval() {
  return this->inc_val;
}

void ptpmgr::adjust(int64_t val) {
  this->offset.value += (__int128) val;
}

void ptpmgr::set_incval(uint64_t inc) {
  this->inc_val = inc;
}

}
