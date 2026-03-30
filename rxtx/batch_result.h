// rxtx/batch_result.h
#ifndef RXTX_BATCH_RESULT_H_
#define RXTX_BATCH_RESULT_H_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    const uint16_t count = batch_->Count();
    for (uint16_t i = 0; i < count; ++i) {
      if constexpr (N > 0) {
        if (i + N < count) {
          Packet::from(batch_->Data()[i + N]).Prefetch();
        }
      }
      Packet& pkt = Packet::from(batch_->Data()[i]);
      fn(pkt, results_[i]);
    }
  }

  template <uint16_t N = 0, typename Fn>
  void PrefetchFilter(Fn&& fn, BatchType& fail_over_batch) {
    static_assert(
        std::is_invocable_r_v<bool, Fn&, Packet*, T&, uint16_t>,
        "BatchResult::PrefetchFilter requires fn(Packet*, T&, uint16_t idx) -> bool");
    assert(batch_ != &fail_over_batch);

    uint16_t keep_write = 0;
    uint16_t fail_write = fail_over_batch.Count();
    const uint16_t original_count = batch_->Count();
    for (uint16_t i = 0; i < original_count; ++i) {
      if constexpr (N > 0) {
        if (i + N < original_count) {
          Packet::from(batch_->Data()[i + N]).Prefetch();
        }
      }

      Packet* pkt = &Packet::from(batch_->Data()[i]);
      T built_result{};
      if (std::invoke(fn, pkt, built_result, i)) {
        batch_->Data()[keep_write] = batch_->Data()[i];
        results_[keep_write] = std::move(built_result);
        ++keep_write;
        continue;
      }

      assert(fail_write < BatchSize);
      fail_over_batch.Data()[fail_write++] = batch_->Data()[i];
    }

    batch_->SetCount(keep_write);
    fail_over_batch.SetCount(fail_write);
  }

 private:
  BatchType* batch_;
  std::array<T, BatchSize> results_;
};

template <typename T, std::size_t N, uint16_t BatchSize, bool SafeMode = false,
          typename Fn>
void Classify(Batch<BatchSize, SafeMode>& input_batch, Fn&& fn,
              const std::array<BatchResult<T, BatchSize, SafeMode>*, N>&
                  outputs) {
  static_assert(N > 0, "Classify requires at least one output lane");
  static_assert(std::is_default_constructible_v<T>,
                "Classify requires default-constructible result type T");
  static_assert(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>,
                "Classify requires move-assignable or copy-assignable T");
  static_assert(std::is_invocable_r_v<std::size_t, Fn&, Packet*, T&>,
                "Classify requires fn(Packet*, T&) -> lane_index");

  std::array<uint16_t, N> lane_write{};
  for (std::size_t lane = 0; lane < N; ++lane) {
    assert(outputs[lane] != nullptr);
    Batch<BatchSize, SafeMode>* out_batch = outputs[lane]->GetBatch();
    assert(out_batch != nullptr);
    assert(out_batch != &input_batch);
    lane_write[lane] = out_batch->Count();
    for (std::size_t other = 0; other < lane; ++other) {
      assert(outputs[other]->GetBatch() != out_batch);
    }
  }

  const uint16_t original_count = input_batch.Count();
  for (uint16_t i = 0; i < original_count; ++i) {
    Packet* pkt = &Packet::from(input_batch.Data()[i]);
    T result{};
    const std::size_t lane = std::invoke(fn, pkt, result);
    assert(lane < N);

    BatchResult<T, BatchSize, SafeMode>* out = outputs[lane];
    Batch<BatchSize, SafeMode>* out_batch = out->GetBatch();
    const uint16_t dst = lane_write[lane]++;
    assert(dst < BatchSize);
    out_batch->Data()[dst] = input_batch.Data()[i];
    out->ResultAt(dst) = std::move(result);
  }

  input_batch.SetCount(0);
  for (std::size_t lane = 0; lane < N; ++lane) {
    outputs[lane]->GetBatch()->SetCount(lane_write[lane]);
  }
}

}  // namespace rxtx

#endif  // RXTX_BATCH_RESULT_H_
