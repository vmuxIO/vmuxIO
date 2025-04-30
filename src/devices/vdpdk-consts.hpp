#pragma once

#include <cstddef>
#include <cstdint>

namespace VDPDK_CONSTS {
  constexpr size_t REGION_SIZE = 0x1000;

  constexpr size_t TX_DESC_SIZE = 0x28;
  constexpr uint16_t TX_FLAG_AVAIL = 1;
  // Set if buffer is attached to an mbuf
  constexpr uint16_t TX_FLAG_ATTACHED = 1 << 1;
  constexpr uint16_t TX_FLAG_NEXT = 1 << 2;

  constexpr size_t RX_DESC_SIZE = 0x20;
  constexpr uint16_t RX_FLAG_AVAIL = 1;

  constexpr size_t MAX_RX_QUEUES = 4;
};
