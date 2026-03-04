// rxtx/packet.h
#ifndef RXTX_PACKET_H_
#define RXTX_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <rte_mbuf.h>
#include <rte_prefetch.h>

#include "rxtx/packet_metadata.h"

namespace rxtx {

// Size of the rte_mbuf structure: 2 cache lines
inline constexpr std::size_t kMbufStructSize = 128;

// Cache line size
inline constexpr std::size_t kCacheLineSize = 64;

// Metadata region size — sized to hold PacketMetadata.
inline constexpr std::size_t kMetadataSize = sizeof(PacketMetadata);

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

  // Access the PacketMetadata stored after the mbuf.
  PacketMetadata& Metadata() { return metadata_; }
  const PacketMetadata& Metadata() const { return metadata_; }

  // Prefetch packet data and metadata for upcoming access.
  // Defined inline so the compiler can interleave prefetch instructions
  // with the caller's loop body. Kept as a named function so prefetch
  // targets can be tuned in one place.
  void Prefetch() {
    rte_prefetch0(Data());
    rte_prefetch0(&metadata_);
  }

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

  friend struct PacketMetadata;

  rte_mbuf mbuf_;              // occupies first 128 bytes
  PacketMetadata metadata_;    // metadata region immediately after mbuf
};

}  // namespace rxtx

#endif  // RXTX_PACKET_H_
