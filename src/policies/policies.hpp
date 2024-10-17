#pragma once

#include <map>
#include <mutex>
#include "util.hpp"

class SwitchPolicy {
  std::map<uint64_t, int> switch_rules; // mac -> vm_id

public:
  /// Return true if rule may be added
  bool add_switch_rule(int vm_id, uint8_t dst_addr[6], uint16_t dst_queue) {
    uint64_t mac = Util::macToInt(dst_addr);
    if (auto search = this->switch_rules.find(mac); search != this->switch_rules.end()) {
      // mac already exists
      if (search->second == vm_id) {
        // mac already used by us. Whatever then...
        return true;
      } else {
        // mac already used by someone else! Deny!
        return false;
      }
    }

    // accept new rule
    this->switch_rules[mac] = vm_id;
    return true;
  }
};

class GlobalPolicies {
public:
  std::mutex mutex;
  SwitchPolicy switchPolicy;
};

