#ifndef INDIRECT_TABLE_SLOT_ARRAY_H_
#define INDIRECT_TABLE_SLOT_ARRAY_H_

#include <cassert>
#include <cstdint>
#include <functional>

#include <rte_malloc.h>

#include "absl/status/status.h"
#include "indirect_table/functors.h"
#include "indirect_table/value_slot.h"
#include "rxtx/rcu_hash_table.h"

namespace indirect_table {

template <typename Value,
          typename ValueHash = std::hash<Value>,
          typename ValueEqual = std::equal_to<Value>>
class SlotArray {
 public:
  static constexpr uint32_t kInvalidId = UINT32_MAX;

  struct Config {
    uint32_t capacity = 0;      // max slots
    uint32_t bucket_count = 0;  // reverse hash table buckets (power of 2)
    const char* name = "";      // rte_zmalloc tag
  };

  SlotArray() = default;

  ~SlotArray() {
    if (slots_ != nullptr) {
      rte_free(slots_);
      slots_ = nullptr;
    }
    if (free_stack_ != nullptr) {
      rte_free(free_stack_);
      free_stack_ = nullptr;
    }
  }

  SlotArray(const SlotArray&) = delete;
  SlotArray& operator=(const SlotArray&) = delete;

  absl::Status Init(const Config& config) {
    capacity_ = config.capacity;

    // Allocate slots array via rte_zmalloc (hugepage-backed, cache-line aligned).
    slots_ = static_cast<ValueSlot<Value>*>(
        rte_zmalloc(config.name,
                    static_cast<size_t>(capacity_) * sizeof(ValueSlot<Value>),
                    alignof(ValueSlot<Value>)));
    if (slots_ == nullptr) {
      return absl::InternalError("rte_zmalloc failed for slots array");
    }
    // Placement-construct each slot so that atomics and hooks are initialized.
    for (uint32_t i = 0; i < capacity_; ++i) {
      new (&slots_[i]) ValueSlot<Value>();
    }

    // Allocate free stack array.
    free_stack_ = static_cast<uint32_t*>(
        rte_zmalloc(config.name,
                    static_cast<size_t>(capacity_) * sizeof(uint32_t),
                    alignof(uint32_t)));
    if (free_stack_ == nullptr) {
      rte_free(slots_);
      slots_ = nullptr;
      return absl::InternalError("rte_zmalloc failed for free stack");
    }

    // Initialize free stack with IDs 0..capacity-1.
    // Push in reverse order so that ID 0 is popped first.
    free_top_ = capacity_;
    for (uint32_t i = 0; i < capacity_; ++i) {
      free_stack_[i] = capacity_ - 1 - i;
    }

    // Construct the reverse map with the configured bucket count.
    reverse_map_ = ReverseMap(config.bucket_count);

    return absl::OkStatus();
  }

  // --- Control-plane mutation API ---

  // Dedup-aware: if value exists in reverse map, AddRef and return existing ID.
  // Otherwise allocate new slot, write value, insert into reverse map.
  // Returns kInvalidId if full.
  uint32_t FindOrAllocate(const Value& value) {
    // Step 1: Check reverse map for existing value.
    ValueSlot<Value>* existing = reverse_map_.Find(value);
    if (existing != nullptr) {
      uint32_t id = SlotIndex(existing);
      AddRef(id);
      return id;
    }

    // Step 2: Allocate new slot from free stack.
    if (free_top_ == 0) return kInvalidId;
    --free_top_;
    uint32_t new_id = free_stack_[free_top_];

    // Step 3: Initialize slot.
    slots_[new_id].value = value;
    slots_[new_id].refcount.store(1, std::memory_order_release);

    // Step 4: Insert into reverse map (intrusive — slot IS the node).
    reverse_map_.Insert(&slots_[new_id]);

    return new_id;
  }

  // Raw allocation without dedup. Returns kInvalidId if full.
  uint32_t Allocate() {
    if (free_top_ == 0) return kInvalidId;
    --free_top_;
    uint32_t id = free_stack_[free_top_];
    slots_[id].refcount.store(1, std::memory_order_release);
    return id;
  }

  // Return slot to free stack, remove from reverse map.
  // Precondition: refcount == 0.
  void Deallocate(uint32_t id) {
    assert(id < capacity_);
    assert(slots_[id].refcount.load(std::memory_order_relaxed) == 0);

    // Remove from reverse map (intrusive unlink).
    reverse_map_.Remove(&slots_[id]);

    // Return ID to free stack.
    free_stack_[free_top_] = id;
    ++free_top_;
  }

  // Increment refcount. Precondition: refcount > 0.
  void AddRef(uint32_t id) {
    assert(id < capacity_);
    uint32_t prev = slots_[id].refcount.load(std::memory_order_relaxed);
    assert(prev > 0);
    slots_[id].refcount.store(prev + 1, std::memory_order_release);
  }

  // Decrement refcount. Returns true when it hits 0.
  // Caller must then Deallocate().
  bool Release(uint32_t id) {
    assert(id < capacity_);
    uint32_t prev = slots_[id].refcount.load(std::memory_order_relaxed);
    assert(prev > 0);
    uint32_t next = prev - 1;
    slots_[id].refcount.store(next, std::memory_order_release);
    return next == 0;
  }

  // Lookup value in reverse hash table. Returns kInvalidId if not found.
  uint32_t FindByValue(const Value& value) const {
    const ValueSlot<Value>* slot = reverse_map_.Find(value);
    if (slot == nullptr) return kInvalidId;
    return SlotIndex(slot);
  }

  // Update value in-place and rehash in reverse map.
  void UpdateValue(uint32_t id, const Value& new_value) {
    assert(id < capacity_);
    assert(slots_[id].refcount.load(std::memory_order_relaxed) > 0);

    // Remove old value from reverse map.
    reverse_map_.Remove(&slots_[id]);

    // Write new value.
    slots_[id].value = new_value;

    // Re-insert with new hash.
    reverse_map_.Insert(&slots_[id]);
  }

  // Read current refcount.
  uint32_t RefCount(uint32_t id) const {
    assert(id < capacity_);
    return slots_[id].refcount.load(std::memory_order_relaxed);
  }

  // Iterate all in-use slots (linear scan, refcount > 0).
  // fn(uint32_t id, const Value& value) called for each.
  template <typename Fn>
  void ForEachInUse(Fn&& fn) const {
    for (uint32_t i = 0; i < capacity_; ++i) {
      if (slots_[i].refcount.load(std::memory_order_relaxed) > 0) {
        fn(i, slots_[i].value);
      }
    }
  }

  // --- PMD-safe lockless read ---

  // Direct array index. Returns pointer into stable rte_zmalloc'd array.
  Value* Get(uint32_t id) {
    assert(id < capacity_);
    return &slots_[id].value;
  }

  const Value* Get(uint32_t id) const {
    assert(id < capacity_);
    return &slots_[id].value;
  }

  uint32_t capacity() const { return capacity_; }

  uint32_t used_count() const { return capacity_ - free_top_; }

 private:
  // Compute slot index from pointer.
  uint32_t SlotIndex(const ValueSlot<Value>* slot) const {
    return static_cast<uint32_t>(slot - slots_);
  }

  ValueSlot<Value>* slots_ = nullptr;     // rte_zmalloc'd array
  uint32_t* free_stack_ = nullptr;        // array-based free-ID stack
  uint32_t free_top_ = 0;
  uint32_t capacity_ = 0;

  // Intrusive reverse hash table: Value → ValueSlot* (no value duplication)
  using ReverseMap = rxtx::RcuHashTable<ValueSlot<Value>,
                                         &ValueSlot<Value>::reverse_hook,
                                         Value,
                                         ValueSlotKeyExtractor<Value>,
                                         ValueHash, ValueEqual>;
  ReverseMap reverse_map_;
};

}  // namespace indirect_table

#endif  // INDIRECT_TABLE_SLOT_ARRAY_H_
