#ifndef RXTX_RCU_HASH_TABLE_H_
#define RXTX_RCU_HASH_TABLE_H_

// RcuHashTable — intrusive hash table with per-bucket locking and lockless
// reads.  Each bucket is cache-line aligned and contains a spinlock, a
// seqlock version counter, kSlotsPerBucket signature/chain-head pairs, and
// an overflow chain head for when all sig slots are occupied.
//
// Shares IntrusiveRcuListHook with IntrusiveRcuList so nodes can participate
// in either a standalone list or a hash table (but not both simultaneously).
//
// Chain operations and retire logic are delegated to IntrusiveRcuList's
// static API — no duplication.
//
// Two configuration shapes (same as IntrusiveRcuList):
//   (bucket_count, rcu_manager)                  — grace period + deferred
//   (bucket_count, rcu_manager, pmd_retire_state) — PMD job retire
//
// Reads (Find, ForEach) are lockless.  Writes (Insert, Remove, Retire*) take
// the per-bucket spinlock.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_pause.h>

#include "rcu/intrusive_rcu_list.h"
#include "rcu/pmd_retire_state.h"
#include "rcu/rcu_manager.h"

namespace rxtx {

inline constexpr std::size_t kSlotsPerBucket = 4;
inline constexpr uint16_t kEmptySig = 0;

template <typename T,
          rcu::IntrusiveRcuListHook T::*HookMember,
          typename Key,
          typename KeyExtractor,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class RcuHashTable {
  // Reuse chain / retire logic from IntrusiveRcuList.
  using Chain = rcu::IntrusiveRcuList<T, HookMember>;

 public:
  using RetireFn = std::function<void(T*)>;
  struct PrefetchContext {
    std::size_t bucket_idx = 0;
    uint16_t sig = kEmptySig;
  };

  RcuHashTable() = default;

  // No retire support.
  explicit RcuHashTable(std::size_t bucket_count)
      : bucket_count_(bucket_count), bucket_mask_(bucket_count - 1) {
    assert(bucket_count > 0 && (bucket_count & bucket_mask_) == 0);
    AllocBuckets();
  }

  // RcuManager-only: grace period + deferred retire.
  RcuHashTable(std::size_t bucket_count, rcu::RcuManager* rcu_manager)
      : bucket_count_(bucket_count),
        bucket_mask_(bucket_count - 1),
        rcu_manager_(rcu_manager) {
    assert(bucket_count > 0 && (bucket_count & bucket_mask_) == 0);
    assert(rcu_manager != nullptr);
    AllocBuckets();
  }

  // RcuManager + PmdRetireState: PMD job retire.
  RcuHashTable(std::size_t bucket_count, rcu::RcuManager* rcu_manager,
               rcu::PmdRetireState* pmd_retire_state)
      : bucket_count_(bucket_count),
        bucket_mask_(bucket_count - 1),
        rcu_manager_(rcu_manager),
        pmd_retire_state_(pmd_retire_state) {
    assert(bucket_count > 0 && (bucket_count & bucket_mask_) == 0);
    assert(rcu_manager != nullptr);
    assert(pmd_retire_state != nullptr);
    AllocBuckets();
  }

  ~RcuHashTable() {
    if (buckets_ != nullptr) {
      rte_free(buckets_);
    }
  }

  RcuHashTable(const RcuHashTable&) = delete;
  RcuHashTable& operator=(const RcuHashTable&) = delete;

  // --- Lockless read operations --------------------------------------------

  void Prefetch(const Key& key, PrefetchContext& ctx_out) const {
    ctx_out = SplitHash(hash_(key));
    if (buckets_ == nullptr) return;
    __builtin_prefetch(&buckets_[ctx_out.bucket_idx], 0, 1);
  }

  T* FindWithPrefetch(const Key& key, const PrefetchContext& ctx) const {
    if (buckets_ == nullptr) return nullptr;
    const Bucket* bucket = &buckets_[ctx.bucket_idx];
    auto match = [&](const T& node) {
      return key_equal_(key_extractor_(node), key);
    };

    for (;;) {
      uint32_t v = BeginRead(bucket);

      T* result = nullptr;
      for (std::size_t i = 0; i < kSlotsPerBucket; ++i) {
        if (bucket->sigs[i] == ctx.sig) {
          result = Chain::ChainFindIf(bucket->heads[i], match);
          if (result != nullptr) break;
        }
      }
      if (result == nullptr) {
        result = Chain::ChainFindIf(bucket->overflow, match);
      }

      if (ValidateRead(bucket, v)) return result;
    }
  }

  T* Find(const Key& key) const {
    return FindWithPrefetch(key, SplitHash(hash_(key)));
  }

  template <typename Fn>
  void ForEach(Fn&& fn) const {
    for (std::size_t b = 0; b < bucket_count_; ++b) {
      const Bucket* bucket = &buckets_[b];
      for (;;) {
        uint32_t v = BeginRead(bucket);

        for (std::size_t i = 0; i < kSlotsPerBucket; ++i) {
          if (bucket->sigs[i] != kEmptySig) {
            Chain::ChainForEach(bucket->heads[i], fn);
          }
        }
        Chain::ChainForEach(bucket->overflow, fn);

        if (ValidateRead(bucket, v)) break;
      }
    }
  }

  // --- Write operations (take per-bucket lock) -----------------------------

  bool Insert(T* item) {
    assert(item != nullptr);
    const Key& key = key_extractor_(*item);
    std::size_t raw_hash = hash_(key);
    auto [bucket_idx, sig] = SplitHash(raw_hash);
    Bucket* bucket = &buckets_[bucket_idx];

    auto match = [&](const T& node) {
      return key_equal_(key_extractor_(node), key);
    };

    LockBucket(bucket);
    BeginWrite(bucket);

    // Check existing sig slots for matching signature.
    std::size_t empty_slot = kSlotsPerBucket;
    for (std::size_t i = 0; i < kSlotsPerBucket; ++i) {
      if (bucket->sigs[i] == sig) {
        if (Chain::ChainFindIf(bucket->heads[i], match)) {
          EndWrite(bucket);
          UnlockBucket(bucket);
          return false;  // duplicate
        }
        Chain::ChainInsert(bucket->heads[i], item);
        EndWrite(bucket);
        UnlockBucket(bucket);
        return true;
      }
      if (bucket->sigs[i] == kEmptySig && empty_slot == kSlotsPerBucket) {
        empty_slot = i;
      }
    }

    // Check overflow for duplicate.
    if (Chain::ChainFindIf(bucket->overflow, match)) {
      EndWrite(bucket);
      UnlockBucket(bucket);
      return false;
    }

    // Use an empty sig slot if available.
    if (empty_slot < kSlotsPerBucket) {
      bucket->sigs[empty_slot] = sig;
      Chain::ChainInsert(bucket->heads[empty_slot], item);
      EndWrite(bucket);
      UnlockBucket(bucket);
      return true;
    }

    // All sig slots occupied — insert into overflow chain.
    Chain::ChainInsert(bucket->overflow, item);
    EndWrite(bucket);
    UnlockBucket(bucket);
    return true;
  }

  bool Remove(T* item) {
    assert(item != nullptr);
    const Key& key = key_extractor_(*item);
    std::size_t raw_hash = hash_(key);
    auto [bucket_idx, sig] = SplitHash(raw_hash);
    Bucket* bucket = &buckets_[bucket_idx];

    LockBucket(bucket);
    BeginWrite(bucket);

    for (std::size_t i = 0; i < kSlotsPerBucket; ++i) {
      if (bucket->sigs[i] == sig) {
        if (Chain::ChainRemove(bucket->heads[i], item)) {
          if (bucket->heads[i].load(std::memory_order_relaxed) == nullptr) {
            bucket->sigs[i] = kEmptySig;
          }
          EndWrite(bucket);
          UnlockBucket(bucket);
          return true;
        }
      }
    }

    bool removed = Chain::ChainRemove(bucket->overflow, item);
    EndWrite(bucket);
    UnlockBucket(bucket);
    return removed;
  }

  // --- Retire functions (remove + delegate to Chain statics) ---------------

  void RemoveAndRetireGracePeriod(T* item, RetireFn retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;
    Chain::RetireGracePeriod(rcu_manager_, item, std::move(retire_fn));
  }

  void RemoveAndRetireDeferred(T* item, RetireFn retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;
    Chain::RetireDeferred(rcu_manager_, item, std::move(retire_fn));
  }

  void RemoveAndRetirePmdJob(T* item, RetireFn retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ != nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;
    Chain::RetirePmdJob(pmd_retire_state_, item, std::move(retire_fn));
  }

  // --- Accessors -----------------------------------------------------------

  std::size_t bucket_count() const { return bucket_count_; }

 private:
  // ---------------------------------------------------------------------------
  // Bucket — cache-line aligned.
  //
  //   lock      4 B   std::atomic<uint32_t> spinlock (0 = unlocked)
  //   version   4 B   seqlock counter (even = stable, odd = writer active)
  //   sigs      8 B   uint16_t[4] hash signatures (0 = empty)
  //   heads    32 B   std::atomic<Hook*>[4] chain heads
  //   overflow  8 B   std::atomic<Hook*> overflow chain head
  //   pad       8 B
  //            -----
  //            64 B   == RTE_CACHE_LINE_SIZE
  // ---------------------------------------------------------------------------
  struct alignas(RTE_CACHE_LINE_SIZE) Bucket {
    std::atomic<uint32_t> lock{0};
    std::atomic<uint32_t> version{0};
    uint16_t sigs[kSlotsPerBucket]{};
    std::atomic<rcu::IntrusiveRcuListHook*> heads[kSlotsPerBucket]{};
    std::atomic<rcu::IntrusiveRcuListHook*> overflow{nullptr};
  };

  static_assert(sizeof(Bucket) == RTE_CACHE_LINE_SIZE,
                "Bucket must be exactly one cache line");

  // --- Hash splitting ------------------------------------------------------

  using HashSplit = PrefetchContext;

  static HashSplit SplitHash(std::size_t raw_hash, std::size_t bucket_mask) {
    std::size_t h = FinalizeHash(raw_hash);
    uint16_t sig = static_cast<uint16_t>(h >> 48);
    if (sig == kEmptySig) sig = 1;
    return {h & bucket_mask, sig};
  }

  static std::size_t FinalizeHash(std::size_t h) {
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return h;
  }

  HashSplit SplitHash(std::size_t raw_hash) const {
    return SplitHash(raw_hash, bucket_mask_);
  }

  // --- Bucket allocation ---------------------------------------------------

  void AllocBuckets() {
    std::size_t sz = bucket_count_ * sizeof(Bucket);
    buckets_ = static_cast<Bucket*>(
        rte_zmalloc(nullptr, sz, RTE_CACHE_LINE_SIZE));
    assert(buckets_ != nullptr);
    for (std::size_t i = 0; i < bucket_count_; ++i) {
      new (&buckets_[i]) Bucket();
    }
  }

  // --- Spinlock ------------------------------------------------------------

  static void LockBucket(Bucket* b) {
    while (b->lock.exchange(1, std::memory_order_acquire) != 0) {
      while (b->lock.load(std::memory_order_relaxed) != 0) {
        rte_pause();
      }
    }
  }

  static void UnlockBucket(Bucket* b) {
    b->lock.store(0, std::memory_order_release);
  }

  // --- Seqlock version protocol --------------------------------------------

  static void BeginWrite(Bucket* b) {
    b->version.fetch_add(1, std::memory_order_release);
  }

  static void EndWrite(Bucket* b) {
    b->version.fetch_add(1, std::memory_order_release);
  }

  static uint32_t BeginRead(const Bucket* b) {
    uint32_t v;
    do {
      v = b->version.load(std::memory_order_acquire);
    } while (v & 1);
    return v;
  }

  static bool ValidateRead(const Bucket* b, uint32_t v) {
    return b->version.load(std::memory_order_acquire) == v;
  }

  // --- Members -------------------------------------------------------------

  Bucket* buckets_ = nullptr;
  std::size_t bucket_count_ = 0;
  std::size_t bucket_mask_ = 0;
  rcu::RcuManager* rcu_manager_ = nullptr;
  rcu::PmdRetireState* pmd_retire_state_ = nullptr;

  [[no_unique_address]] KeyExtractor key_extractor_;
  [[no_unique_address]] Hash hash_;
  [[no_unique_address]] KeyEqual key_equal_;
};

}  // namespace rxtx

#endif  // RXTX_RCU_HASH_TABLE_H_
