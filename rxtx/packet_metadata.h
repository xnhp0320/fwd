// rxtx/packet_metadata.h
#ifndef RXTX_PACKET_METADATA_H_
#define RXTX_PACKET_METADATA_H_

#include <cstdint>

namespace rxtx {

// Forward declaration — full definition in rxtx/packet.h.
class Packet;

// Stores either an IPv4 (32-bit) or IPv6 (128-bit) address.
// The active member is determined by PacketMetadata::flags bit 0.
union IpAddress {
  uint32_t v4;
  uint8_t v6[16];
};

// Lightweight error codes returned by PacketMetadata::Parse.
enum class ParseResult : uint8_t {
  kOk = 0,
  kChecksumError,
  kTooShort,
  kLengthMismatch,
  kMalformedHeader,
  kUnsupportedVersion,
  kUdpLengthMismatch,
};

// Flag bits stored in PacketMetadata::flags.
enum MetaFlag : uint64_t {
  kFlagIpv6          = 1u << 0,  // Addresses are IPv6
  kFlagFragment      = 1u << 1,  // Packet is a fragment (IPv4 or IPv6)
  kFlagIpv4Options   = 1u << 2,  // IPv4 options present (IHL > 5)
  kFlagIpv6ExtHeaders = 1u << 3, // IPv6 extension headers present
};

// Parsed 5-tuple + VNI extracted from a raw packet.
//
// Layout (56 bytes, fits in one cache line):
//   Offset  0: src_ip        (IpAddress, 16 bytes)
//   Offset 16: dst_ip        (IpAddress, 16 bytes)
//   Offset 32: src_port      (uint16_t,   2 bytes)
//   Offset 34: dst_port      (uint16_t,   2 bytes)
//   Offset 36: vni           (uint32_t,   4 bytes)
//   Offset 40: protocol      (uint8_t,    1 byte)
//   Offset 41: vlan_count    (uint8_t,    1 byte)
//   Offset 42: frag_offset   (uint16_t,   2 bytes)
//   Offset 44: outer_vlan_id (uint16_t,   2 bytes)
//   Offset 46: inner_vlan_id (uint16_t,   2 bytes)
//   Offset 48: flags         (uint64_t,   8 bytes)
struct PacketMetadata {
  IpAddress src_ip;
  IpAddress dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t vni;
  uint8_t protocol;
  uint8_t vlan_count;
  uint16_t frag_offset;
  uint16_t outer_vlan_id;
  uint16_t inner_vlan_id;
  uint64_t flags;

  // Returns true if the stored addresses are IPv6.
  bool IsIpv6() const { return flags & kFlagIpv6; }

  // Returns true if the packet is a fragment (IPv4 or IPv6).
  bool IsFragment() const { return flags & kFlagFragment; }

  // Returns true if IPv4 options are present (IHL > 5).
  bool HasIpv4Options() const { return flags & kFlagIpv4Options; }

  // Returns true if IPv6 extension headers were present.
  bool HasIpv6ExtHeaders() const { return flags & kFlagIpv6ExtHeaders; }

  // Parse packet headers, populate this metadata and mbuf layer lengths.
  // pkt must reference a valid Packet with accessible mbuf data.
  static ParseResult Parse(Packet& pkt, PacketMetadata& meta);

 private:
  friend class Packet;
};

static_assert(sizeof(PacketMetadata) == 56,
              "PacketMetadata must be 56 bytes (one cache line)");

}  // namespace rxtx

#endif  // RXTX_PACKET_METADATA_H_
