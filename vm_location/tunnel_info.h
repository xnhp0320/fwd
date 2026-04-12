// vm_location/tunnel_info.h
#ifndef VM_LOCATION_TUNNEL_INFO_H_
#define VM_LOCATION_TUNNEL_INFO_H_

#include <cstddef>
#include <cstring>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

#include "rxtx/packet_metadata.h"

namespace vm_location {

struct TunnelInfo {
  rxtx::IpAddress ip;
  bool is_ipv6;
};

// Hash functor for TunnelInfo.
// IPv4-optimized: hashes only 4 bytes when is_ipv6 is false,
// all 16 bytes when is_ipv6 is true.
struct TunnelInfoHash {
  std::size_t operator()(const TunnelInfo& t) const {
    if (t.is_ipv6) {
      return absl::HashOf(
          absl::string_view(reinterpret_cast<const char*>(t.ip.v6), 16),
          t.is_ipv6);
    }
    return absl::HashOf(t.ip.v4, t.is_ipv6);
  }
};

// Equality functor for TunnelInfo.
// Compares only v4 for IPv4, memcmp all 16 bytes for IPv6.
// Returns false when is_ipv6 differs.
struct TunnelInfoEqual {
  bool operator()(const TunnelInfo& a, const TunnelInfo& b) const {
    if (a.is_ipv6 != b.is_ipv6) return false;
    if (a.is_ipv6) {
      return std::memcmp(a.ip.v6, b.ip.v6, 16) == 0;
    }
    return a.ip.v4 == b.ip.v4;
  }
};

}  // namespace vm_location

#endif  // VM_LOCATION_TUNNEL_INFO_H_
