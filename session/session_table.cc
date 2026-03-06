#include "session/session_table.h"

#include <rte_cycles.h>
#include <rte_errno.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_mempool.h>
#include <rte_rcu_qsbr.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace session {

SessionTable::~SessionTable() {
  if (hash_ != nullptr) {
    ForEach([](const SessionKey&, SessionEntry*) { return true; });
    rte_hash_free(hash_);
  }
  if (pool_ != nullptr) {
    rte_mempool_free(pool_);
  }
}

absl::Status SessionTable::Init(const Config& config,
                                 struct rte_rcu_qsbr* qsbr_var) {
  if (config.capacity == 0) {
    return absl::InvalidArgumentError("capacity must be > 0");
  }

  capacity_ = config.capacity;

  // Create rte_mempool for SessionEntry objects.
  // MPMC (default) — any thread can get/put.
  // No obj_init — version survives recycle.
  std::string pool_name = absl::StrCat(config.name, "_pool");
  pool_ = rte_mempool_create(pool_name.c_str(), config.capacity,
                              sizeof(SessionEntry), 256, 0, nullptr, nullptr,
                              nullptr, nullptr, SOCKET_ID_ANY, 0);
  if (pool_ == nullptr) {
    return absl::ResourceExhaustedError(
        absl::StrCat("rte_mempool_create failed: ", rte_strerror(rte_errno)));
  }

  // Create rte_hash with lock-free readers + multi-writer.
  std::string hash_name = absl::StrCat(config.name, "_hash");
  struct rte_hash_parameters params = {};
  params.name = hash_name.c_str();
  params.entries = config.capacity;
  params.key_len = sizeof(SessionKey);
  params.hash_func = rte_hash_crc;
  params.hash_func_init_val = 0;
  params.socket_id = SOCKET_ID_ANY;
  params.extra_flag = RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF |
                      RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD;

  hash_ = rte_hash_create(&params);
  if (hash_ == nullptr) {
    rte_mempool_free(pool_);
    pool_ = nullptr;
    return absl::InternalError(
        absl::StrCat("rte_hash_create failed: ", rte_strerror(rte_errno)));
  }

  // Attach RCU QSBR for deferred internal slot reclamation.
  if (qsbr_var != nullptr) {
    struct rte_hash_rcu_config rcu_cfg = {};
    rcu_cfg.v = qsbr_var;
    rcu_cfg.mode = RTE_HASH_QSBR_MODE_DQ;
    rcu_cfg.dq_size = 0;
    rcu_cfg.trigger_reclaim_limit = 0;
    rcu_cfg.max_reclaim_size = 0;

    int ret = rte_hash_rcu_qsbr_add(hash_, &rcu_cfg);
    if (ret != 0) {
      rte_hash_free(hash_);
      rte_mempool_free(pool_);
      hash_ = nullptr;
      pool_ = nullptr;
      return absl::InternalError("rte_hash_rcu_qsbr_add failed");
    }
  }

  return absl::OkStatus();
}

SessionEntry* SessionTable::Lookup(const SessionKey& key) const {
  void* data = nullptr;
  int ret = rte_hash_lookup_data(hash_, &key, &data);
  if (ret < 0) {
    return nullptr;
  }
  return static_cast<SessionEntry*>(data);
}

SessionEntry* SessionTable::Insert(const SessionKey& key) {
  // Check if key already exists (lock-free read).
  SessionEntry* existing = Lookup(key);
  if (existing != nullptr) {
    return existing;
  }

  // Allocate from pool.
  void* obj = nullptr;
  int ret = rte_mempool_get(pool_, &obj);
  if (ret != 0) {
    return nullptr;  // pool exhausted
  }

  SessionEntry* entry = static_cast<SessionEntry*>(obj);

  // Initialize entry. version=1 (first incarnation), timestamp=now.
  entry->version.store(1, std::memory_order_relaxed);
  entry->timestamp.store(rte_rdtsc(), std::memory_order_relaxed);

  // Insert into rte_hash.
  ret = rte_hash_add_key_data(hash_, &key, entry);
  if (ret < 0) {
    rte_mempool_put(pool_, entry);
    return nullptr;
  }

  return entry;
}

absl::Status SessionTable::Delete(const SessionKey& key) {
  void* data = nullptr;
  int ret = rte_hash_lookup_data(hash_, &key, &data);
  if (ret < 0) {
    return absl::NotFoundError("session key not found");
  }

  SessionEntry* entry = static_cast<SessionEntry*>(data);

  // Bump version to invalidate all cached references.
  BumpVersion(entry);

  // Remove from hash. With QSBR attached, internal slot reclamation
  // is deferred until all PMD threads pass a quiescent state.
  rte_hash_del_key(hash_, &key);

  // Return entry to pool.
  ReturnToPool(entry);

  return absl::OkStatus();
}

int32_t SessionTable::Count() const {
  return rte_hash_count(hash_);
}

void SessionTable::BumpVersion(SessionEntry* entry) {
  uint32_t v = entry->version.load(std::memory_order_relaxed);
  entry->version.store(v + 1, std::memory_order_relaxed);
}

void SessionTable::ReturnToPool(SessionEntry* entry) {
  rte_mempool_put(pool_, entry);
}

}  // namespace session
