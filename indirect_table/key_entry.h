#ifndef INDIRECT_TABLE_KEY_ENTRY_H_
#define INDIRECT_TABLE_KEY_ENTRY_H_

#include <cstdint>

#include "rcu/intrusive_rcu_list.h"

namespace indirect_table {

// Allocated from rte_mempool. Stored in the key RcuHashTable.
template <typename Key>
struct KeyEntry {
  rcu::IntrusiveRcuListHook hook;  // for key RcuHashTable chain
  Key key;
  uint32_t value_id;               // index into SlotArray
};

}  // namespace indirect_table

#endif  // INDIRECT_TABLE_KEY_ENTRY_H_
