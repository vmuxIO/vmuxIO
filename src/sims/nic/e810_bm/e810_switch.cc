#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cassert>
#include <iostream>
#include <netinet/if_ether.h>
using namespace std;
#include "sims/nic/e810_bm/e810_base_wrapper.h"
#include "sims/nic/e810_bm/e810_bm.h"
#include "sims/nic/e810_bm/headers.h"

namespace e810 {

bool e810_switch::add_rule(struct ice_aqc_sw_rules_elem *add_sw_rules) {
  uint32_t action_type = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_TYPE_M) >> ICE_SINGLE_ACT_TYPE_S;
  uint32_t queue_id = (add_sw_rules->pdata.lkup_tx_rx.act & ICE_SINGLE_ACT_Q_INDEX_M) >> ICE_SINGLE_ACT_Q_INDEX_S;
  uint16_t recipe_idx = 0; // default recipe (only one we support)
  if (
      add_sw_rules->type != ICE_AQC_SW_RULES_T_LKUP_RX || // we only support simple sw rules
      action_type != ICE_SINGLE_ACT_TO_Q || // we only support forwarding to a queue
      add_sw_rules->pdata.lkup_tx_rx.recipe_id != recipe_idx || // we only support recipe 0 (hardcoded in firmware)
      add_sw_rules->pdata.lkup_tx_rx.hdr_len != this->recipies[recipe_idx].size() // depends on the recipe 0 (should be 56)
      ) {
    return false; // rule too complicated: not supported by this emulator
  }

  struct ethhdr *hdr = (struct ethhdr*)(add_sw_rules->pdata.lkup_tx_rx.hdr);
  auto driver = this->dev.vmux->device->driver;
  auto device_id = this->dev.vmux->device->device_id;

  // mac rules
  // uint64_t dst_mac = 0xFFFFFFFFFFFF & *(uint64_t*)(hdr->h_dest);
  // this->mac_rules[dst_mac] = queue_id;
  // bool installed_rule = this->dev.vmux->device->add_switch_rule(device_id, hdr->h_dest, queue_id - this->dev.vsi0_first_queue);

  // ethertype rules
  uint16_t etype = be16toh(hdr->h_proto);
  this->ethertype_rules[etype] = queue_id;
  std::vector<uint8_t> *rule = new std::vector<uint8_t>();
  for (size_t i = 0; i < this->recipies[recipe_idx].size(); i++) {
    uint8_t mask = this->recipies[recipe_idx][i];
    uint8_t byte = mask & add_sw_rules->pdata.lkup_tx_rx.hdr[i];
    rule->push_back(byte);
  }
  this->rules.push_back(rule);
  this->rules_recipe_idx.push_back(recipe_idx);
  this->rules_dst_queues.push_back(queue_id);
  bool installed_rule = this->dev.vmux->device->add_switch_etype_rule(device_id, etype, queue_id - this->dev.vsi0_first_queue);

  return installed_rule;
}

/**
 * set queue if a switching rule applies
 */
void e810_switch::select_queue(const void* data, size_t len, uint16_t* queue) {
  // assume firmware recipe 0
  // match ethertype, src_mac, dst_ac, vlan, logical port, ...
  if (len < sizeof(struct ethhdr)) {
    return;
  }
  struct ethhdr* packet_hdr = (struct ethhdr*) data;

  // check mac rules
  uint64_t dst_mac = 0xFFFFFFFFFFFF & *(uint64_t*)(packet_hdr->h_dest);
  if (auto search = this->mac_rules.find(dst_mac); search != this->mac_rules.end()) {
    *queue = search->second; // return map entry, if it exists
    return;
  }

  for (size_t rule_idx = 0; rule_idx < this->rules.size(); rule_idx++) {
    uint16_t recipe_idx = this->rules_recipe_idx[rule_idx];
    auto rule = *this->rules[rule_idx];
    // based on recipies[recipe_idx], data, and rule, we can determine if the rule matches
    bool rule_matches = true;
    for (size_t i = 0; i < this->recipies[recipe_idx].size(); i++) {
      uint8_t mask = this->recipies[recipe_idx][i];
      uint8_t masked = mask & ((uint8_t*)data)[i];
      bool byte_matches = masked == rule[i];
      if (!byte_matches) {
        rule_matches = false;
        break;
      }
    }
    if (rule_matches) {
      *queue = this->rules_dst_queues[rule_idx];
      return;
    }
  }

  // uint16_t ethertype = be16toh(packet_hdr->h_proto);
  // if (auto search = this->ethertype_rules.find(dst_mac); search != this->ethertype_rules.end()) {
  //   *queue = search->second; // return map entry, if it exists
  //   return;
  // }
}

void e810_switch::print_sw_rule(struct ice_aqc_sw_rules_elem *add_sw_rules) {
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

}
