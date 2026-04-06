#ifndef INDIRECT_TABLE_FUNCTORS_H_
#define INDIRECT_TABLE_FUNCTORS_H_

#include "indirect_table/key_entry.h"
#include "indirect_table/value_slot.h"

namespace indirect_table {

// Key extractor for the key RcuHashTable: KeyEntry → Key
template <typename Key>
struct KeyEntryKeyExtractor {
  const Key& operator()(const KeyEntry<Key>& entry) const {
    return entry.key;
  }
};

// Key extractor for the reverse RcuHashTable: ValueSlot → Value
template <typename Value>
struct ValueSlotKeyExtractor {
  const Value& operator()(const ValueSlot<Value>& slot) const {
    return slot.value;
  }
};

}  // namespace indirect_table

#endif  // INDIRECT_TABLE_FUNCTORS_H_
