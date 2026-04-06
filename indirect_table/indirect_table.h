#ifndef INDIRECT_TABLE_INDIRECT_TABLE_H_
#define INDIRECT_TABLE_INDIRECT_TABLE_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <new>
#include <string>

#include <rte_errno.h>
#include <rte_mempool.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "indirect_table/functors.h"
#include "indirect_table/key_entry.h"
#include "indirect_table/slot_array.h"
#include "rcu/rcu_manager.h"
#include "rcu/rcu_retire.h"
#include "rxtx/rcu_hash_table.h"

namespace indirect_table {

template <typename Key, typename Value,
          typename KeyHash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>,
          typename ValueHash = std::hash<Value>,
          typename ValueEqual = std::equal_to<Value>>
class IndirectTable {
 public:
  using KeyHashTable = rxtx::RcuHashTable<
      KeyEntry<Key>, &KeyEntry<Key>::hook,
      Key, KeyEntryKeyExtractor<Key>,
      KeyHash, KeyEqual>;
  using PrefetchContext = typename KeyHashTable::PrefetchContext;
  using SlotArrayType = SlotArray<Value, ValueHash, ValueEqual>;

  struct Config {
    uint32_t value_capacity = 0;      // SlotArray capacity
    uint32_t value_bucket_count = 0;  // reverse map buckets (power of 2)
    uint32_t key_capacity = 0;        // rte_mempool / max KeyEntries
    uint32_t key_bucket_count = 0;    // key hash table buckets (power of 2)
    const char* name = "";
  };

  IndirectTable() = default;

  ~IndirectTable() {
    if (key_pool_ != nullptr) {
      rte_mempool_free(key_pool_);
      key_pool_ = nullptr;
    }
  }

  IndirectTable(const IndirectTable&) = delete;
  IndirectTable& operator=(const IndirectTable&) = delete;

  absl::Status Init(const Config& config, rcu::RcuManager* rcu_manager) {
    rcu_manager_ = rcu_manager;

    // Initialize the key RcuHashTable with the configured bucket count.
    key_table_ = KeyHashTable(config.key_bucket_count);

    // Create rte_mempool for KeyEntry allocation.
    std::string pool_name = absl::StrCat(config.name, "_key_pool");
    key_pool_ = rte_mempool_create(pool_name.c_str(), config.key_capacity,
                                   sizeof(KeyEntry<Key>), 0, 0, nullptr,
                                   nullptr, nullptr, nullptr, SOCKET_ID_ANY,
                                   0);
    if (key_pool_ == nullptr) {
      return absl::ResourceExhaustedError(
          absl::StrCat("rte_mempool_create failed for key pool: ",
                       rte_strerror(rte_errno)));
    }

    // Initialize the SlotArray.
    typename SlotArrayType::Config sa_config;
    sa_config.capacity = config.value_capacity;
    sa_config.bucket_count = config.value_bucket_count;
    sa_config.name = config.name;
    auto status = slot_array_.Init(sa_config);
    if (!status.ok()) {
      rte_mempool_free(key_pool_);
      key_pool_ = nullptr;
      return status;
    }

    return absl::OkStatus();
  }

  // --- Control-plane mutation API ---

  // Insert key→value. Dedup via FindOrAllocate.
  // Returns value_id on success, kInvalidId on failure.
  uint32_t Insert(const Key& key, const Value& value) {
    // 1. Check for duplicate key
    if (key_table_.Find(key) != nullptr) return SlotArrayType::kInvalidId;

    // 2. Get or create value slot (dedup via reverse hash table)
    uint32_t value_id = slot_array_.FindOrAllocate(value);
    if (value_id == SlotArrayType::kInvalidId) return SlotArrayType::kInvalidId;

    // 3. Allocate KeyEntry from mempool
    void* raw = nullptr;
    if (rte_mempool_get(key_pool_, &raw) != 0) {
      // Rollback: release the refcount we just acquired
      if (slot_array_.Release(value_id)) {
        slot_array_.Deallocate(value_id);
      }
      return SlotArrayType::kInvalidId;
    }
    auto* entry = new (raw) KeyEntry<Key>{};
    entry->key = key;
    entry->value_id = value_id;

    // 4. Insert into key hash table (refcount already incremented)
    bool ok = key_table_.Insert(entry);
    assert(ok);
    (void)ok;

    return value_id;
  }

  // Insert key referencing existing value_id. AddRef.
  bool InsertWithId(const Key& key, uint32_t value_id) {
    // 1. Check for duplicate key
    if (key_table_.Find(key) != nullptr) return false;

    // 2. Increment refcount for the existing value slot
    slot_array_.AddRef(value_id);

    // 3. Allocate KeyEntry from mempool
    void* raw = nullptr;
    if (rte_mempool_get(key_pool_, &raw) != 0) {
      // Rollback: undo the AddRef
      slot_array_.Release(value_id);
      return false;
    }
    auto* entry = new (raw) KeyEntry<Key>{};
    entry->key = key;
    entry->value_id = value_id;

    // 4. Insert into key hash table
    bool ok = key_table_.Insert(entry);
    assert(ok);
    (void)ok;

    return true;
  }

  // Remove key. RCU-retire KeyEntry; release refcount after grace period.
  bool Remove(const Key& key) {
    KeyEntry<Key>* entry = key_table_.Find(key);
    if (entry == nullptr) return false;

    uint32_t value_id = entry->value_id;
    key_table_.Remove(entry);

    // Defer cleanup until RCU grace period completes
    rcu::RetireViaGracePeriod(rcu_manager_, entry,
        [this, value_id](KeyEntry<Key>* e) {
          e->~KeyEntry<Key>();
          rte_mempool_put(key_pool_, e);
          if (slot_array_.Release(value_id)) {
            slot_array_.Deallocate(value_id);
          }
        });

    return true;
  }

  // Update value in-place at given slot ID.
  void UpdateValue(uint32_t value_id, const Value& value) {
    slot_array_.UpdateValue(value_id, value);
  }

  // Iterate all key entries. fn(const Key&, uint32_t value_id).
  template <typename Fn>
  void ForEachKey(Fn&& fn) const {
    key_table_.ForEach([&fn](const KeyEntry<Key>& entry) {
      fn(entry.key, entry.value_id);
    });
  }

  // --- PMD-safe lockless read API ---

  KeyEntry<Key>* Find(const Key& key) const {
    return key_table_.Find(key);
  }

  void Prefetch(const Key& key, PrefetchContext& ctx) const {
    key_table_.Prefetch(key, ctx);
  }

  KeyEntry<Key>* FindWithPrefetch(const Key& key,
                                   const PrefetchContext& ctx) const {
    return key_table_.FindWithPrefetch(key, ctx);
  }

  // Direct access to SlotArray for PMD Get() calls.
  const SlotArrayType& slot_array() const { return slot_array_; }
  SlotArrayType& slot_array() { return slot_array_; }

 private:
  KeyHashTable key_table_;
  struct rte_mempool* key_pool_ = nullptr;
  SlotArrayType slot_array_;
  rcu::RcuManager* rcu_manager_ = nullptr;
};

}  // namespace indirect_table

#endif  // INDIRECT_TABLE_INDIRECT_TABLE_H_
