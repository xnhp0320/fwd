// rxtx/packet.h
#ifndef RXTX_PACKET_H_
#define RXTX_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <rte_mbuf.h>

namespace rxtx {

// Size of the rte_mbuf structure: 2 cache lines
inline constexpr std::size_t kMbufStructSize = 128;

// Cache line size
inline constexpr std::size_t kCacheLineSize = 64;

// Metadata region size (zero initially, reserved for future use)
inline constexpr std::size_t kMetadataSize = 0;

class Packet {
 public:
  // Construct a Packet in-place over an existing rte_mbuf.
  // Returns a reference to the Packet occupying the same memory.
  static Packet& from(rte_mbuf* mbuf) {
    return *new (mbuf) Packet;
  }

  // Pointer to the start of packet payload data
  uint8_t* Data() {
    return rte_pktmbuf_mtod(&mbuf_, uint8_t*);
  }

  const uint8_t* Data() const {
    return rte_pktmbuf_mtod(&mbuf_, const uint8_t*);
  }

  // Packet data length in bytes
  uint16_t Length() const {
    return rte_pktmbuf_data_len(&mbuf_);
  }

  // Access the underlying rte_mbuf
  rte_mbuf* Mbuf() { return &mbuf_; }
  const rte_mbuf* Mbuf() const { return &mbuf_; }

  // Free the underlying mbuf back to its mempool
  void Free() {
    rte_pktmbuf_free(&mbuf_);
  }

  // Non-copyable
  Packet(const Packet&) = delete;
  Packet& operator=(const Packet&) = delete;

  // Static assertions on layout
  static_assert(kMetadataSize + kMbufStructSize <= kMbufStructSize + RTE_PKTMBUF_HEADROOM,
                "Metadata region must fit within mbuf headroom");

 private:
  // Default constructor is private — only accessible via placement new in from().
  // This prevents external default construction while allowing from() to work.
  Packet() = default;

  rte_mbuf mbuf_;  // occupies first 128 bytes
  // Metadata_Region would follow here when kMetadataSize > 0
  // alignas(kCacheLineSize) std::array<uint8_t, kMetadataSize> metadata_;
};

}  // namespace rxtx

#endif  // RXTX_PACKET_H_
