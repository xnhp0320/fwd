// rxtx/lookup_entry.h
#ifndef RXTX_LOOKUP_ENTRY_H_
#define RXTX_LOOKUP_ENTRY_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "boost/intrusive/slist.hpp"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"

namespace rxtx {

// A single flow entry stored in the slab and referenced by the hash set.
// Aligned to kCacheLineSize (64 bytes) so that all key fields plus the
// intrusive hook fit in a single cache line.
//
// Memory layout (64 bytes):
//   Offset  0: hook       (slist_member_hook, 8 bytes — one pointer)
//   Offset  8: src_ip     (IpAddress, 16 bytes)
//   Offset 24: dst_ip     (IpAddress, 16 bytes)
//   Offset 40: src_port   (uint16_t, 2 bytes)
//   Offset 42: dst_port   (uint16_t, 2 bytes)
//   Offset 44: protocol   (uint8_t, 1 byte)
//   Offset 45: flags      (uint8_t, 1 byte)
//   Offset 46: [padding]  (2 bytes)
//   Offset 48: vni        (uint32_t, 4 bytes)
//   Offset 52: [padding]  (12 bytes to fill cache line)
struct alignas(kCacheLineSize) LookupEntry {
  boost::intrusive::slist_member_hook<> hook;

  IpAddress src_ip;
  IpAddress dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint8_t protocol;
  uint8_t flags;       // bit 0: 1 = IPv6, 0 = IPv4
  // 2 bytes implicit padding
  uint32_t vni;
  // 12 bytes implicit padding to 64-byte alignment

  bool IsIpv6() const { return flags & 0x01; }

  // Populate this entry's key fields from a PacketMetadata.
  // Extracts src_ip, dst_ip, src_port, dst_port, protocol, vni,
  // and the IPv6 flag from meta.flags.
  void FromMetadata(const PacketMetadata& meta) {
    src_ip = meta.src_ip;
    dst_ip = meta.dst_ip;
    src_port = meta.src_port;
    dst_port = meta.dst_port;
    protocol = meta.protocol;
    vni = meta.vni;
    flags = static_cast<uint8_t>(meta.flags & kFlagIpv6);
  }
};

static_assert(sizeof(LookupEntry) == 64,
              "LookupEntry must be exactly one cache line (64 bytes)");
static_assert(alignof(LookupEntry) == 64,
              "LookupEntry must be cache-line aligned");

// Custom hash functor that dereferences LookupEntry pointers.
// IPv4-optimized: hashes only 4 bytes per address when flags indicate IPv4.
struct LookupEntryHash {
  using is_transparent = void;

  std::size_t operator()(const LookupEntry* p) const {
    if (p->IsIpv6()) {
      return absl::HashOf(
          absl::string_view(reinterpret_cast<const char*>(p->src_ip.v6), 16),
          absl::string_view(reinterpret_cast<const char*>(p->dst_ip.v6), 16),
          p->src_port, p->dst_port, p->protocol, p->vni, p->flags);
    }
    return absl::HashOf(
        p->src_ip.v4, p->dst_ip.v4,
        p->src_port, p->dst_port, p->protocol, p->vni, p->flags);
  }
};

// Custom equality functor that dereferences LookupEntry pointers.
// IPv4-optimized: compares only 4 bytes per address when flags indicate IPv4.
struct LookupEntryEq {
  using is_transparent = void;

  bool operator()(const LookupEntry* a, const LookupEntry* b) const {
    if (a->flags != b->flags) return false;
    if (a->src_port != b->src_port) return false;
    if (a->dst_port != b->dst_port) return false;
    if (a->protocol != b->protocol) return false;
    if (a->vni != b->vni) return false;
    if (a->IsIpv6()) {
      return std::memcmp(a->src_ip.v6, b->src_ip.v6, 16) == 0 &&
             std::memcmp(a->dst_ip.v6, b->dst_ip.v6, 16) == 0;
    }
    return a->src_ip.v4 == b->src_ip.v4 &&
           a->dst_ip.v4 == b->dst_ip.v4;
  }
};

}  // namespace rxtx

#endif  // RXTX_LOOKUP_ENTRY_H_
