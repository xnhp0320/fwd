// rxtx/batch_result.h
#ifndef RXTX_BATCH_RESULT_H_
#define RXTX_BATCH_RESULT_H_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "rxtx/batch.h"

namespace rxtx {

template <typename T, uint16_t BatchSize, bool SafeMode = false>
class BatchResult {
 public:
  using BatchType = Batch<BatchSize, SafeMode>;

  explicit BatchResult(BatchType* batch) : batch_(batch) {
    assert(batch_ != nullptr);
    static_assert(std::is_default_constructible_v<T>,
                  "BatchResult<T> requires default-constructible T");
    static_assert(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>,
                  "BatchResult<T> requires move-assignable or copy-assignable T");
  }

  BatchType* GetBatch() { return batch_; }
  const BatchType* GetBatch() const { return batch_; }

  void SetBatch(BatchType* batch) {
    assert(batch != nullptr);
    batch_ = batch;
  }

  uint16_t Count() const { return batch_->count_; }
  static constexpr uint16_t Capacity() { return BatchSize; }

  T* Data() { return results_.data(); }
  const T* Data() const { return results_.data(); }

  T& ResultAt(uint16_t i) { return results_[i]; }
  const T& ResultAt(uint16_t i) const { return results_[i]; }

  Packet& PacketAt(uint16_t i) { return Packet::from(batch_->mbufs_[i]); }
  const Packet& PacketAt(uint16_t i) const {
    return Packet::from(batch_->mbufs_[i]);
  }

  template <typename BuildFn>
  void Build(BuildFn&& fn) {
    for (uint16_t i = 0; i < batch_->count_; ++i) {
      Packet& pkt = Packet::from(batch_->mbufs_[i]);
      fn(pkt, results_[i]);
    }
  }

  template <typename Fn>
  void ForEach(Fn&& fn) {
    for (uint16_t i = 0; i < batch_->count_; ++i) {
      Packet& pkt = Packet::from(batch_->mbufs_[i]);
      fn(pkt, results_[i]);
    }
  }

  template <uint16_t N, typename Fn>
  void PrefetchForEach(Fn&& fn) {
    for (uint16_t i = 0; i < batch_->count_; ++i) {
      if constexpr (N > 0) {
        if (i + N < batch_->count_) {
          Packet::from(batch_->mbufs_[i + N]).Prefetch();
        }
      }
      Packet& pkt = Packet::from(batch_->mbufs_[i]);
      fn(pkt, results_[i]);
    }
  }

  template <std::size_t K, typename LaneFn>
  void Classify(
      LaneFn&& lane_fn,
      const std::array<BatchType*, K - 1>& slow_batches,
      const std::array<BatchResult<T, BatchSize, SafeMode>*, K - 1>&
          slow_results) {
    static_assert(K >= 1, "Classify requires at least one lane");

    std::array<uint16_t, K - 1> slow_write{};
    for (std::size_t lane = 0; lane + 1 < K; ++lane) {
      assert(slow_batches[lane] != nullptr);
      assert(slow_results[lane] != nullptr);
      assert(slow_results[lane]->GetBatch() == slow_batches[lane]);
      slow_write[lane] = slow_batches[lane]->count_;
    }

    uint16_t fast_write = 0;
    const uint16_t original_count = batch_->count_;
    for (uint16_t i = 0; i < original_count; ++i) {
      Packet& pkt = Packet::from(batch_->mbufs_[i]);
      T& result = results_[i];
      const std::size_t lane = static_cast<std::size_t>(lane_fn(pkt, result));
      assert(lane < K);
      if (lane == 0) {
        if (fast_write != i) {
          batch_->mbufs_[fast_write] = batch_->mbufs_[i];
          results_[fast_write] = std::move(results_[i]);
        }
        ++fast_write;
        continue;
      }

      const std::size_t slow_idx = lane - 1;
      const uint16_t dst = slow_write[slow_idx]++;
      slow_batches[slow_idx]->mbufs_[dst] = batch_->mbufs_[i];
      slow_results[slow_idx]->results_[dst] = std::move(results_[i]);
    }

    batch_->count_ = fast_write;
    for (std::size_t lane = 0; lane + 1 < K; ++lane) {
      slow_batches[lane]->count_ = slow_write[lane];
    }
  }

 private:
  BatchType* batch_;
  std::array<T, BatchSize> results_;
};

}  // namespace rxtx

#endif  // RXTX_BATCH_RESULT_H_
