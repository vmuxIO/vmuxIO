#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cassert>
#include <iostream>
using namespace std;
#include "sims/nic/e810_bm/e810_base_wrapper.h"
#include "sims/nic/e810_bm/e810_bm.h"

namespace i40e {

class e810_switch {
  public:
  static void print_sw_rule(struct ice_aqc_sw_rules_elem *add_sw_rules) {
    if (add_sw_rules->type == ICE_AQC_SW_RULES_T_LKUP_RX || add_sw_rules->type == ICE_AQC_SW_RULES_T_LKUP_TX) {
      bool is_rx = add_sw_rules->type == ICE_AQC_SW_RULES_T_LKUP_RX;
      cout << "   " << 
        (is_rx ? "rx" : "tx") <<
        " lookup recipe_id=" << add_sw_rules->pdata.lkup_tx_rx.recipe_id <<
        (is_rx ? " src_port=" : " src_vsi=") << add_sw_rules->pdata.lkup_tx_rx.src << 
        " rule_index=" << add_sw_rules->pdata.lkup_tx_rx.index <<
        logger::endl;
      cout << "   " << 
        "hdr_len=" << add_sw_rules->pdata.lkup_tx_rx.hdr_len << 
        logger::endl;
      uint32_t action_type = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_TYPE_M) >> ICE_SINGLE_ACT_TYPE_S;
      uint32_t vsi_id_list = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_VSI_ID_M) >> ICE_SINGLE_ACT_VSI_ID_S;
      uint32_t queue_id = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_Q_INDEX_M) >> ICE_SINGLE_ACT_Q_INDEX_S;
      bool large_action = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_PTR_BIT);
      if (action_type == ICE_SINGLE_ACT_VSI_FORWARDING) {
        cout << "   action VSI id/list=" << vsi_id_list <<
          logger::endl;
      } else if (action_type == ICE_SINGLE_ACT_TO_Q) {
        cout << "   action Queue idx=" << queue_id << 
          " region=? hdr:" << logger::endl;
        Util::hexdump(add_sw_rules->pdata.lkup_tx_rx.hdr, add_sw_rules->pdata.lkup_tx_rx.hdr_len);
      } else if (action_type == ICE_SINGLE_ACT_PRUNE && !large_action) {
        cout << "   action Prune vsi/list=?" << logger::endl;
      } else if (action_type == ICE_SINGLE_ACT_PTR && large_action) {
        cout << "   action Pointer (large action) ?=?" << logger::endl;
      } else if (action_type == ICE_SINGLE_ACT_OTHER_ACTS) {
        cout << "   action other ?=?" << logger::endl;
      }
    } else if (add_sw_rules->type == ICE_AQC_SW_RULES_T_LG_ACT) {
      cout << "   large action" << logger::endl;
    } else if (add_sw_rules->type == ICE_AQC_SW_RULES_T_VSI_LIST_SET) {
      cout << "   vsi list set" << logger::endl;
    } else if (add_sw_rules->type == ICE_AQC_SW_RULES_T_VSI_LIST_CLEAR) {
      cout << "   vsi list clear" << logger::endl;
    } else if (add_sw_rules->type == ICE_AQC_SW_RULES_T_PRUNE_LIST_SET) {
      cout << "   vsi prune list set" << logger::endl;
    } else if (add_sw_rules->type == ICE_AQC_SW_RULES_T_PRUNE_LIST_CLEAR) {
      cout << "   vsi prune list clear" << logger::endl;
    }
  }
};

}
