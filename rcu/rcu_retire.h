#ifndef RCU_RCU_RETIRE_H_
#define RCU_RCU_RETIRE_H_

// Free helper functions for RCU-deferred reclamation.  These are decoupled
// from any particular data structure — call them after unlinking an item
// from an IntrusiveRcuList, RcuHashTable, or any other RCU-protected
// container.

#include <cassert>
#include <functional>

#include <rte_rcu_qsbr.h>

#include "rcu/deferred_work_item.h"
#include "rcu/pmd_retire_state.h"
#include "rcu/rcu_manager.h"

namespace rcu {

template <typename T>
using RetireFn = std::function<void(T*)>;

// Schedule reclamation after the current RCU grace period completes.
// Use from the control-plane thread.
template <typename T, typename Fn>
void RetireViaGracePeriod(RcuManager* mgr, T* item, Fn&& retire_fn) {
  assert(mgr != nullptr && item != nullptr);
  auto status = mgr->CallAfterGracePeriod(
      [item, fn = std::forward<Fn>(retire_fn)]() mutable { fn(item); });
  assert(status.ok());
  (void)status;
}

// Post a deferred retire via the MPSC queue.
// Use from any PMD thread (wait-free).
template <typename T, typename Fn>
void RetireViaDeferred(RcuManager* mgr, T* item, Fn&& retire_fn) {
  assert(mgr != nullptr && item != nullptr);
  struct rte_rcu_qsbr* qsbr = mgr->GetQsbrVar();
  assert(qsbr != nullptr);
  auto* work = new DeferredWorkItem();
  work->token = rte_rcu_qsbr_start(qsbr);
  work->callback = [item, fn = std::forward<Fn>(retire_fn)]() mutable {
    fn(item);
  };
  mgr->PostDeferredWork(work);
}

// Enqueue a retire into the PmdRetireState's pending list.
// Use from the owning PMD thread.
template <typename T, typename Fn>
void RetireViaPmdJob(PmdRetireState* state, T* item, Fn&& retire_fn) {
  assert(state != nullptr && item != nullptr);
  state->AddPendingRetire(
      [item, fn = std::forward<Fn>(retire_fn)]() mutable { fn(item); });
}

}  // namespace rcu

#endif  // RCU_RCU_RETIRE_H_
