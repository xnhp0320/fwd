#ifndef SESSION_SESSION_ENTRY_H_
#define SESSION_SESSION_ENTRY_H_

#include <atomic>
#include <cstdint>

namespace session {

// Session state stored in the rte_mempool.
// Aligned to 64 bytes to avoid false sharing between entries.
//
// Memory layout (64 bytes):
//   Offset  0: version    (atomic<uint32_t>, 4 bytes)
//   Offset  4: [padding]  (4 bytes)
//   Offset  8: timestamp  (atomic<uint64_t>, 8 bytes)
//   Offset 16: [reserved] (48 bytes for future fields)
//
// Thread safety:
//   - version: written by control plane only (relaxed load + store),
//              read by PMD threads (relaxed load)
//   - timestamp: written by PMD threads (relaxed store),
//                read by control plane (relaxed load)
struct alignas(64) SessionEntry {
  std::atomic<uint32_t> version{0};
  std::atomic<uint64_t> timestamp{0};

  // Padding to fill cache line. Reserved for future session state
  // (e.g., byte counters, TCP state, NAT mappings).
  uint8_t reserved_[48] = {};
};

static_assert(sizeof(SessionEntry) == 64,
              "SessionEntry must be exactly one cache line");
static_assert(alignof(SessionEntry) == 64,
              "SessionEntry must be cache-line aligned");

}  // namespace session

#endif  // SESSION_SESSION_ENTRY_H_
