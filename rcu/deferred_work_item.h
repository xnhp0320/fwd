#ifndef RCU_DEFERRED_WORK_ITEM_H_
#define RCU_DEFERRED_WORK_ITEM_H_

#include <atomic>
#include <cstdint>
#include <functional>

namespace rcu {

// Type-erased move-only callable. We use std::function here for simplicity;
// a production system might use a custom MoveOnlyFunction to avoid the
// std::function copy requirement. For our use case, lambdas capturing
// shared_ptr or raw pointers are the common pattern, so std::function works.
using DeferredAction = std::function<void()>;

struct DeferredWorkItem {
  // Intrusive link for the MPSC queue. Written by producers (XCHG),
  // read by the consumer. Must be the first field for cache-line alignment.
  std::atomic<DeferredWorkItem*> next{nullptr};

  // RCU token from rte_rcu_qsbr_start(). The grace period is complete
  // when rte_rcu_qsbr_check(qsbr_var, token, false) returns true.
  uint64_t token{0};

  // The callback to invoke once the grace period completes.
  DeferredAction callback;
};

}  // namespace rcu

#endif  // RCU_DEFERRED_WORK_ITEM_H_
