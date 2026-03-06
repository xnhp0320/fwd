#ifndef SESSION_SESSION_TABLE_H_
#define SESSION_SESSION_TABLE_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "session/session_entry.h"
#include "session/session_key.h"

#include <rte_hash.h>

struct rte_hash;
struct rte_mempool;
struct rte_rcu_qsbr;

namespace session {

class SessionTable {
 public:
  struct Config {
    uint32_t capacity = 0;
    const char* name = "session_table";
  };

  SessionTable() = default;
  ~SessionTable();

  SessionTable(const SessionTable&) = delete;
  SessionTable& operator=(const SessionTable&) = delete;

  absl::Status Init(const Config& config, struct rte_rcu_qsbr* qsbr_var);
  SessionEntry* Lookup(const SessionKey& key) const;
  SessionEntry* Insert(const SessionKey& key);
  absl::Status Delete(const SessionKey& key);

  template <typename Fn>
  uint32_t ForEach(Fn fn);

  int32_t Count() const;
  uint32_t capacity() const { return capacity_; }

 private:
  void BumpVersion(SessionEntry* entry);
  void ReturnToPool(SessionEntry* entry);

  struct rte_hash* hash_ = nullptr;
  struct rte_mempool* pool_ = nullptr;
  uint32_t capacity_ = 0;
};

// ForEach is a template, so it must be defined in the header.
template <typename Fn>
uint32_t SessionTable::ForEach(Fn fn) {
  const void* key_ptr = nullptr;
  void* data = nullptr;
  uint32_t iter = 0;
  uint32_t visited = 0;

  while (rte_hash_iterate(hash_, &key_ptr, &data, &iter) >= 0) {
    const SessionKey* sk = static_cast<const SessionKey*>(key_ptr);
    SessionEntry* entry = static_cast<SessionEntry*>(data);
    visited++;

    if (fn(*sk, entry)) {
      BumpVersion(entry);
      rte_hash_del_key(hash_, sk);
      ReturnToPool(entry);
    }
  }

  return visited;
}

}  // namespace session

#endif  // SESSION_SESSION_TABLE_H_
