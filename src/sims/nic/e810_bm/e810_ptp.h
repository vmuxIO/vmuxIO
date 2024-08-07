#pragma once

#include <stdint.h>
#include "sims/nic/e810_bm/util.h"

namespace e810 {

typedef union {
 
 struct {
  uint8_t resv[4];
  uint64_t time;
  uint32_t time_0; // internal time
 };

 struct {
  uint8_t resv2[11];
  uint16_t ts_h_1;
  uint16_t ts_h_0;
  uint8_t ts_l;
  uint32_t resv3;
 }; 
 __int128 value;

} __attribute__((packed)) e810_timestamp_t;

#define E810_TIMESTAMP_MASK (((__int128)0xFFFF'FFFF << 64) | 0xFFFF'FFFF'FFFF'FFFF) // lower 96 bits

#define PTP_GLTSYN_ENA(i) ((i) & 1)
#define PTP_GLTSYN_GET_CMD(i) ((i) & 0xff)
#define PTP_GLTSYN_CMD_SEL_MASTER(i) (((i) & 0x100) >> 0x8)

#define PTP_GLTSYN_CMD_INIT_TIME 0x01 // init TIME registers
#define PTP_GLTSYN_CMD_INIT_INCVAL 0x02 // init INCVAL registers
#define PTP_GLTSYN_CMD_INIT_TIME_INCVAL 0x03 // init TIME and INCVAL registers
#define PTP_GLTSYN_CMD_ADJ_TIME 0x04 // adjust time
#define PTP_GLTSYN_CMD_ADJ_TIME_AFTER_TGT 0x0C
#define PTP_GLTSYN_CMD_READ 0x80 // read time of both timers to their INIT_TIME registers

#define PTP_GLTSYN_SEM_BUSY(i) ((i) & 1)
#define PTP_GLTSYN_SEM_SET_BUSY(i, busy) ((i) | ((busy) & 1))

#define PTP_GLTSYN_CMD_SYNC_INC 0b01
#define PTP_GLTSYN_CMD_SYNC_DEC 0b10
#define PTP_GLTSYN_CMD_SYNC_EXEC 0b11

#define PTP_ATQBAL_IS_ACTIVE(i) (!!((i) & 0x8000'0000))
#define PTP_ATQBAL_REG_INDEX(i) (((i) >> 24) & 0b1111)

// bit 31 must be zero and bits 16:23 must contain upper 8 bits of the TS
#define PTP_ATQBAL_SET_TS(ts) ((uint32_t) (((ts) & 0xff'0000'0000) >> 16)) 
#define PTP_ATQBAH_SET_TS(ts) ((ts) & 0xffff'ffff)

// PTP capabilities
#define CAP_VF_TIMESYNC_ENA (1 << 0)
#define CAP_VF_TIMER_OWNED (1 << 1)
#define CAP_VF_TIMER_ENA (1 << 2)
#define CAP_VF_TS_LL_READ (1 << 28)

#define CAP_PF_TIMER_0_ENA (1 << 25)
#define CAP_PF_TIMER_1_ENA (1 << 26)

#define CAP_PF_TIMER_0_OWNED (1 << 3)
#define CAP_PF_TIMER_1_OWNED (1 << 7)

#define CAP_1588_FLAGS (uint32_t) (CAP_VF_TIMESYNC_ENA | CAP_VF_TIMER_OWNED | \
          CAP_VF_TIMER_ENA | CAP_VF_TS_LL_READ | \
          CAP_PF_TIMER_0_ENA | CAP_PF_TIMER_0_OWNED | \
          CAP_PF_TIMER_1_ENA | CAP_PF_TIMER_1_OWNED )

#define TIMESPEC_TO_NANOS(ts) (((uint64_t) (ts.tv_sec) * 1'000'000'000ULL) + (ts.tv_nsec))

class e810_bm;

class PTPManager {
 protected:
  static const uint64_t CLOCK_HZ = 812500000;

  e810_bm &dev;

  uint64_t last_updated;
  e810_timestamp_t last_val;
  e810_timestamp_t offset;
  uint64_t inc_val;

 public:
  PTPManager(e810_bm &dev);
  
  void set_enabled(uint32_t clock);

  e810_timestamp_t phc_read();
  
  e810_timestamp_t phc_sample_rx(uint16_t portid);
  
  e810_timestamp_t phc_sample_tx(uint16_t portid);

  void phc_write(e810_timestamp_t val);

  void adjust(int64_t val);

  void set_incval(uint64_t inc);
  
  uint64_t get_incval();
};

}
