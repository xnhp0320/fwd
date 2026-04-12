// vm_location/vm_location_key.h
#ifndef VM_LOCATION_VM_LOCATION_KEY_H_
#define VM_LOCATION_VM_LOCATION_KEY_H_

#include <cstddef>
#include <cstring>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

#include "rxtx/packet_metadata.h"

namespace vm_location {

struct VmLocationKey {
  rxtx::IpAddress ip;
  bool is_ipv6;
};

// Hash functor for VmLocationKey.
// IPv4-optimized: hashes only 4 bytes when is_ipv6 is false,
// all 16 bytes when is_ipv6 is true.
struct VmLocationKeyHash {
  std::size_t operator()(const VmLocationKey& k) const {
    if (k.is_ipv6) {
      return absl::HashOf(
          absl::string_view(reinterpret_cast<const char*>(k.ip.v6), 16),
          k.is_ipv6);
    }
    return absl::HashOf(k.ip.v4, k.is_ipv6);
  }
};

// Equality functor for VmLocationKey.
// Compares only v4 for IPv4, memcmp all 16 bytes for IPv6.
// Returns false when is_ipv6 differs.
struct VmLocationKeyEqual {
  bool operator()(const VmLocationKey& a, const VmLocationKey& b) const {
    if (a.is_ipv6 != b.is_ipv6) return false;
    if (a.is_ipv6) {
      return std::memcmp(a.ip.v6, b.ip.v6, 16) == 0;
    }
    return a.ip.v4 == b.ip.v4;
  }
};

}  // namespace vm_location

#endif  // VM_LOCATION_VM_LOCATION_KEY_H_
