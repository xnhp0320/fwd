#ifndef PROCESSOR_PACKET_STATS_H_
#define PROCESSOR_PACKET_STATS_H_

#include <atomic>
#include <cstdint>

namespace processor {

class PacketStats {
 public:
  PacketStats() : packets_(0), bytes_(0) {}

  // Hot path: called by processor after each RX burst.
  // Single-writer per instance (one PMD thread), so we use
  // load + add + store instead of fetch_add to avoid the
  // lock prefix (lock xadd) overhead on x86-64.
  void RecordBatch(uint16_t packet_count, uint64_t byte_count) {
    packets_.store(packets_.load(std::memory_order_relaxed) + packet_count,
                   std::memory_order_relaxed);
    bytes_.store(bytes_.load(std::memory_order_relaxed) + byte_count,
                 std::memory_order_relaxed);
  }

  // Cold path: called by control plane to read counters.
  uint64_t GetPackets() const {
    return packets_.load(std::memory_order_relaxed);
  }

  uint64_t GetBytes() const {
    return bytes_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t> packets_;
  std::atomic<uint64_t> bytes_;
};

}  // namespace processor

#endif  // PROCESSOR_PACKET_STATS_H_
