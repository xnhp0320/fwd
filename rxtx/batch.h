// rxtx/batch.h
#ifndef RXTX_BATCH_H_
#define RXTX_BATCH_H_

#include <cstdint>
#include <rte_mbuf.h>

#include "rxtx/packet.h"

namespace rxtx {

template <uint16_t BatchSize, bool SafeMode = false>
class Batch {
 public:
  Batch() : count_(0) {}

  // Non-copyable
  Batch(const Batch&) = delete;
  Batch& operator=(const Batch&) = delete;

  // Destructor frees all remaining mbufs
  ~Batch() {
    for (uint16_t i = 0; i < count_; ++i) {
      rte_pktmbuf_free(mbufs_[i]);
    }
  }

  // Raw pointer to mbuf array — pass directly to rte_eth_rx_burst / rte_eth_tx_burst
  rte_mbuf** Data() { return mbufs_; }

  // Pointer to count — pass to rte_eth_rx_burst to receive the burst count
  uint16_t* CountPtr() { return &count_; }

  // Current number of packets in the batch
  uint16_t Count() const { return count_; }

  // Set count (used after rte_eth_rx_burst)
  void SetCount(uint16_t count) { count_ = count; }

  // Compile-time capacity
  static constexpr uint16_t Capacity() { return BatchSize; }

  // Release ownership of all mbufs without freeing them. Resets count to 0.
  void Release() {
    count_ = 0;
  }

  // Apply fn to each packet in order [0, count)
  template <typename Fn>
  void ForEach(Fn&& fn) {
    for (uint16_t i = 0; i < count_; ++i) {
      Packet& pkt = Packet::from(mbufs_[i]);
      fn(pkt);
    }
  }
  // Retain packets where fn returns true. Rejected mbufs are NOT freed —
  // they are simply excluded from the compacted result.
  // Retained packets are compacted to contiguous positions [0, new_count).
  template <typename Fn>
  void Filter(Fn&& fn) {
    uint16_t write = 0;
    for (uint16_t i = 0; i < count_; ++i) {
      Packet& pkt = Packet::from(mbufs_[i]);
      if (fn(pkt)) {
        mbufs_[write++] = mbufs_[i];
      }
    }
    count_ = write;
  }
  // Append a Packet's underlying mbuf to the batch.
  // SafeMode=false: void, no bounds check, maximum performance.
  // SafeMode=true: returns bool, checks capacity first.
  auto Append(Packet& pkt) {
    return Append(pkt.Mbuf());
  }

  // Append a raw rte_mbuf* to the batch.
  // SafeMode=false: void, no bounds check, maximum performance.
  // SafeMode=true: returns bool, checks capacity first.
  auto Append(rte_mbuf* mbuf) {
    if constexpr (SafeMode) {
      if (count_ >= BatchSize) {
        return false;
      }
      mbufs_[count_++] = mbuf;
      return true;
    } else {
      mbufs_[count_++] = mbuf;
    }
  }

 private:
  rte_mbuf* mbufs_[BatchSize];
  uint16_t count_;
};

}  // namespace rxtx

#endif  // RXTX_BATCH_H_
