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
#define PTP_GLTSYN_CMD_READ 0x80 // read time of both timers to their INIT_TIME registers

#define PTP_GLTSYN_SEM_BUSY(i) ((i) & 1)
#define PTP_GLTSYN_SEM_SET_BUSY(i, busy) ((i) | ((busy) & 1))


typedef union {
 struct {
  uint64_t time;
  uint32_t time_0;
 };
 struct {
  uint64_t resv : 56;
  uint16_t ts_h_1;
  uint16_t ts_h_0;
  uint8_t ts_l;
 };
 __int128 value : 96;
}__attribute__((packed)) gltsyn_ts_t;


// Helper macros to simplify switch case statements. CASE_N duplicates the provided statement N times, and sets the INDEX integer to the current index.
#define CASE(base, stmt) \
      case (base): {stmt; break;}


#define CASE_2(base, stmt) \
      case (base(0)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1)): {uint32_t INDEX = 1; stmt; break;}


#define CASE_4(base, offset, stmt) \
      case (base(0 + offset)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1 + offset)): {uint32_t INDEX = 1; stmt; break;} \
      case (base(2 + offset)): {uint32_t INDEX = 2; stmt; break;} \
      case (base(3 + offset)): {uint32_t INDEX = 3; stmt; break;}



#define CASE_6(base, offset, stmt) \
      case (base(0 + offset)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1 + offset)): {uint32_t INDEX = 1; stmt; break;} \
      case (base(2 + offset)): {uint32_t INDEX = 2; stmt; break;} \
      case (base(3 + offset)): {uint32_t INDEX = 3; stmt; break;} \
      case (base(4 + offset)): {uint32_t INDEX = 4; stmt; break;} \
      case (base(5 + offset)): {uint32_t INDEX = 5; stmt; break;}


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

