#ifndef INDIRECT_TABLE_VALUE_SLOT_H_
#define INDIRECT_TABLE_VALUE_SLOT_H_

#include <atomic>
#include <cstdint>

#include "rcu/intrusive_rcu_list.h"

namespace indirect_table {

// Cache-line aligned slot in the SlotArray backing array.
// reverse_hook is used by the intrusive reverse hash table (control-plane only).
// refcount is std::atomic for visibility to PMD threads but only mutated by
// control-plane. PMD threads access via Get() which returns &slot.value —
// they never touch hook or refcount.
template <typename Value>
struct alignas(64) ValueSlot {
  rcu::IntrusiveRcuListHook reverse_hook;  // for reverse RcuHashTable chain
  std::atomic<uint32_t> refcount{0};       // 0 = free, >0 = in use
  Value value;                              // user payload
};

}  // namespace indirect_table

#endif  // INDIRECT_TABLE_VALUE_SLOT_H_
