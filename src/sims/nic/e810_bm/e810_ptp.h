#pragma once

#include <stdint.h>

namespace i40e {


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
#define PTP_ATQBAL_SET_TS(i, ts) (((i) & 0x70ff'0000) | ((uint32_t) ((ts) & 0xff'0000'0000) >> 16)) 
#define PTP_ATQBAH_SET_TS(ts) ((ts) & 0xffff'ffff)

// PTP capabilities
//#define CAP_TIME_SYNC_ENA 0b1
//#define CAP_TIMER_OWNED 0b10
//#define CAP_TIMER_ENA 0b100
//#define CAP_SDP_TIME_SYNC 0b111100000000

#define CAP_GPIO_TIME_SYNC (1 << 13)

#define CAP_PF_TIMER_0_OWNED (0b1 << 3)
#define CAP_PF_TIMER_1_OWNED (0b1 << 7)



#define CAP_PF_TIMESYNC_ENA (1 << 24)
#define CAP_PF_TIMER_0_ENA (1 << 25)
#define CAP_PF_TIMER_1_ENA (1 << 26)
#define CAP_PF_LL_TX_SUPPORTED (1 << 28)

#define CAP_1588_FLAGS ((uint32_t) CAP_PF_TIMER_0_OWNED | CAP_PF_TIMER_0_ENA | CAP_PF_TIMESYNC_ENA | CAP_PF_LL_TX_SUPPORTED | \
                        CAP_GPIO_TIME_SYNC )



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
  
  uint64_t inc_get();
};


}

