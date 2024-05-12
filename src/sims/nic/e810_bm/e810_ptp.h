#pragma once

#include "src/sims/nic/e810_bm/e810_base_wrapper.h"


namespace i40e {

#define PTP_GLTSYN_ENA(i) ((i) & 1)
#define PTP_GLTSYN_CMD(i) ((i) & 0xff)
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

#define PTP_ATQBAL_MASK(i) (((i) >> 31) & 1)
#define PTP_ATQBAL_INDEX(i) (((i) >> 24) & 0b1111)
#define PTP_ATQBAL_SET_TS(i, ts) (((i) & 0xff00ffff) | (((ts) & 0xff00000000) >> 16))
#define PTP_ATQBAH_SET_TS(ts) ((ts) & 0xffffffff)


typedef union {
 struct {
  uint8_t resv[4];
  uint64_t time;
  uint32_t time_0;
 };
 struct {
  uint8_t resv2[11];
  uint16_t ts_h_1;
  uint16_t ts_h_0;
  uint8_t ts_l;
 };
 __int128 value;
}__attribute__((packed)) gltsyn_ts_t;


class e810_bm;

class ptpmgr {
 protected:
  static const uint64_t CLOCK_HZ = 812500000;

  e810_bm &dev;

  uint64_t last_updated;
  gltsyn_ts_t last_val;
  gltsyn_ts_t offset;
  uint64_t inc_val;
  bool adj_neg;
  uint64_t adj_val;

 public:
  ptpmgr(e810_bm &dev);

  gltsyn_ts_t update_clock();

  gltsyn_ts_t phc_read();

  void phc_write(gltsyn_ts_t val);

  uint64_t adj_get();

  void adj_set(uint64_t val);

  void inc_set(uint64_t inc);
};


}

