#ifndef SESSION_SESSION_KEY_H_
#define SESSION_SESSION_KEY_H_

#include <cstdint>
#include <cstring>

#include "rxtx/packet_metadata.h"

namespace session {

// Flat key for rte_hash. Must be trivially copyable and memcmp-safe.
// rte_hash compares keys byte-by-byte, so all padding must be zeroed.
//
// Memory layout (44 bytes):
//   Offset  0: src_ip    (IpAddress, 16 bytes)
//   Offset 16: dst_ip    (IpAddress, 16 bytes)
//   Offset 32: src_port  (uint16_t, 2 bytes)
//   Offset 34: dst_port  (uint16_t, 2 bytes)
//   Offset 36: zone_id   (uint32_t, 4 bytes)
//   Offset 40: protocol  (uint8_t, 1 byte)
//   Offset 41: flags     (uint8_t, 1 byte)  — bit 0: IPv6
//   Offset 42: [padding] (2 bytes, must be zero)
//   Total: 44 bytes
struct SessionKey {
  rxtx::IpAddress src_ip;
  rxtx::IpAddress dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t zone_id;
  uint8_t protocol;
  uint8_t flags;       // bit 0: 1 = IPv6, 0 = IPv4
  uint8_t pad_[2];     // explicit padding, must be zero

  // Build a SessionKey from PacketMetadata with a given zone_id.
  // Zeroes the struct first to ensure padding bytes are clean.
  static SessionKey FromMetadata(const rxtx::PacketMetadata& meta,
                                 uint32_t zone_id) {
    SessionKey key;
    std::memset(&key, 0, sizeof(key));
    key.src_ip = meta.src_ip;
    key.dst_ip = meta.dst_ip;
    key.src_port = meta.src_port;
    key.dst_port = meta.dst_port;
    key.zone_id = zone_id;
    key.protocol = meta.protocol;
    key.flags = static_cast<uint8_t>(meta.flags & rxtx::kFlagIpv6);
    return key;
  }
};

static_assert(sizeof(SessionKey) == 44,
              "SessionKey must be 44 bytes for rte_hash");

}  // namespace session

#endif  // SESSION_SESSION_KEY_H_
