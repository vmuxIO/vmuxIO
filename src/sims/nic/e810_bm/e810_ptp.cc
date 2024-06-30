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
    if(clock_gettime(CLOCK_MONOTONIC, &ts)) {
      printf("Error: Could not get unix timestamp. \n");
      return { .value=0 };

    } else {

      uint64_t cycle_now = TIMESPEC_TO_NANOS(ts); // (uint64_t) (((__int128) TIMESPEC_TO_NANOS(ts) *  (__int128) 1'000'000'000'000ULL) / CLOCK_HZ);
      uint64_t cycles_passed = cycle_now - last_updated;

      this->last_updated = cycle_now;
      this->last_val.time += (uint64_t) ((double) cycles_passed);

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

/* Returns the global time and places the 40 bit RX timestamp in tstamp
*/
e810_timestamp_t ptpmgr::phc_sample_rx(uint16_t portid) {

  if (auto e810_dev = dynamic_pointer_cast<E810EmulatedDevice>(this->dev.vmux->device)) {
    uint64_t timestamp = e810_dev->driver->readRxTimestamp(portid);
    return { .time=timestamp, .time_0=0 };

  } else {
    return this->phc_read();
  }
}

/* Returns the global time and places the 40 bit TX timestamp in tstamp
*/
e810_timestamp_t ptpmgr::phc_sample_tx(uint16_t portid) {

  if (auto e810_dev = dynamic_pointer_cast<E810EmulatedDevice>(this->dev.vmux->device)) {
    uint64_t timestamp = e810_dev->driver->readTxTimestamp(portid);
    return { .time=timestamp, .time_0=0 };

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
