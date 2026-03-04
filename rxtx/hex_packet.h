// rxtx/hex_packet.h
// Test utility: converts a hex-encoded string into raw bytes in a DPDK mbuf.
// Used by deterministic unit tests with scapy-generated packet data.
#ifndef RXTX_HEX_PACKET_H_
#define RXTX_HEX_PACKET_H_

#include <cstdint>
#include <cstring>

#include <rte_mbuf.h>

#include "rxtx/packet.h"
#include "rxtx/test_utils.h"

namespace rxtx {
namespace testing {

// Loads a hex-encoded packet string into a DPDK mbuf for use in parser tests.
//
// Usage:
//   HexPacket pkt("4500002800010000400600...", alloc);
//   ASSERT_TRUE(pkt.Valid());
//   auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
class HexPacket {
 public:
  // Construct from hex string. Decodes hex pairs to bytes, allocates an mbuf,
  // and copies the decoded bytes into the mbuf data area.
  // Optional packet_type and ol_flags simulate hardware classification.
  HexPacket(const char* hex, TestMbufAllocator& alloc,
            uint32_t packet_type = 0, uint64_t ol_flags = 0)
      : mbuf_(nullptr), valid_(false), length_(0) {
    if (hex == nullptr) return;

    std::size_t hex_len = std::strlen(hex);

    // Reject odd-length strings.
    if (hex_len % 2 != 0) return;

    // Validate all characters are hex digits.
    for (std::size_t i = 0; i < hex_len; ++i) {
      if (HexVal(hex[i]) < 0) return;
    }

    uint16_t byte_count = static_cast<uint16_t>(hex_len / 2);
    length_ = byte_count;

    // Allocate mbuf with zero initial data_len.
    mbuf_ = alloc.Alloc(RTE_PKTMBUF_HEADROOM, 0);
    if (mbuf_ == nullptr) return;

    // Reserve space and get pointer to write area.
    char* raw = rte_pktmbuf_append(mbuf_, byte_count);
    if (raw == nullptr) {
      // Not enough room in the mbuf — shouldn't happen for normal test packets.
      return;
    }
    uint8_t* dest = reinterpret_cast<uint8_t*>(raw);

    // Decode hex pairs to bytes.
    for (uint16_t i = 0; i < byte_count; ++i) {
      uint8_t hi = static_cast<uint8_t>(HexVal(hex[i * 2]));
      uint8_t lo = static_cast<uint8_t>(HexVal(hex[i * 2 + 1]));
      dest[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    // Set optional hardware classification fields.
    mbuf_->packet_type = packet_type;
    mbuf_->ol_flags = ol_flags;

    valid_ = true;
  }

  // Returns a Packet& suitable for PacketMetadata::Parse().
  // Caller must check Valid() first.
  Packet& GetPacket() {
    return Packet::from(mbuf_);
  }

  // Returns the decoded byte count.
  uint16_t Length() const { return length_; }

  // Returns true if construction succeeded.
  bool Valid() const { return valid_; }

 private:
  // Convert a single hex character to its 0–15 value, or -1 on error.
  static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  }

  rte_mbuf* mbuf_;
  bool valid_;
  uint16_t length_;
};

}  // namespace testing
}  // namespace rxtx

#endif  // RXTX_HEX_PACKET_H_
