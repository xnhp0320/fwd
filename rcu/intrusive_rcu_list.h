#ifndef RCU_INTRUSIVE_RCU_LIST_H_
#define RCU_INTRUSIVE_RCU_LIST_H_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <rte_rcu_qsbr.h>

#include "rcu/deferred_work_item.h"
#include "rcu/pmd_retire_state.h"
#include "rcu/rcu_manager.h"

namespace rcu {

struct IntrusiveRcuListHook {
  std::atomic<IntrusiveRcuListHook*> next{nullptr};
};

template <typename T>
using RetireFn = std::function<void(T*)>;

// ---------------------------------------------------------------------------
// IntrusiveRcuList — intrusive singly-linked list with RCU-deferred reclaim.
//
// Two configuration shapes:
//   (rcu_manager)                    — list without a PMD retire context.
//                                      Retire via RemoveAndRetireGracePeriod
//                                      (control-plane thread) or
//                                      RemoveAndRetireDeferred (PMD thread).
//   (rcu_manager, pmd_retire_state)  — list with a shared PMD retire context.
//                                      Retire via RemoveAndRetirePmdJob.
//                                      The caller manages PmdRetireState
//                                      lifetime and calls RefreshScheduling().
//
// Calling the wrong retire function for the configuration is a programming
// error (assert-fail).
//
// Writers (InsertHead / Remove / Retire*) must be serialized externally.
// ForEach is safe for concurrent read-only traversal.
// ---------------------------------------------------------------------------
template <typename T, IntrusiveRcuListHook T::*HookMember>
class IntrusiveRcuList {
 public:
  // List-only (no retire support).
  IntrusiveRcuList() = default;

  // RcuManager-only: supports RemoveAndRetireGracePeriod (control-plane
  // thread) and RemoveAndRetireDeferred (PMD thread).
  explicit IntrusiveRcuList(RcuManager* rcu_manager)
      : rcu_manager_(rcu_manager) {
    assert(rcu_manager != nullptr);
  }

  // RcuManager + PmdRetireState: supports RemoveAndRetirePmdJob.
  // The PmdRetireState is shared (non-owning) — caller manages its lifetime.
  IntrusiveRcuList(RcuManager* rcu_manager, PmdRetireState* pmd_retire_state)
      : rcu_manager_(rcu_manager), pmd_retire_state_(pmd_retire_state) {
    assert(rcu_manager != nullptr);
    assert(pmd_retire_state != nullptr);
  }

  IntrusiveRcuList(const IntrusiveRcuList&) = delete;
  IntrusiveRcuList& operator=(const IntrusiveRcuList&) = delete;

  // --- Core list operations ------------------------------------------------

  bool InsertHead(T* item) {
    if (item == nullptr) return false;
    IntrusiveRcuListHook* hook = Hook(item);
    hook->next.store(nullptr, std::memory_order_relaxed);

    IntrusiveRcuListHook* old_head = head_.load(std::memory_order_relaxed);
    do {
      hook->next.store(old_head, std::memory_order_relaxed);
    } while (!head_.compare_exchange_weak(old_head, hook,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));
    return true;
  }

  bool Remove(T* item) {
    if (item == nullptr) return false;

    IntrusiveRcuListHook* target = Hook(item);
    IntrusiveRcuListHook* prev = nullptr;
    IntrusiveRcuListHook* curr = head_.load(std::memory_order_acquire);
    while (curr != nullptr) {
      IntrusiveRcuListHook* next = curr->next.load(std::memory_order_acquire);
      if (curr == target) {
        if (prev == nullptr) {
          head_.store(next, std::memory_order_release);
        } else {
          prev->next.store(next, std::memory_order_release);
        }
        curr->next.store(nullptr, std::memory_order_release);
        return true;
      }
      prev = curr;
      curr = next;
    }
    return false;
  }

  template <typename Fn>
  void ForEach(Fn&& fn) const {
    IntrusiveRcuListHook* curr = head_.load(std::memory_order_acquire);
    while (curr != nullptr) {
      fn(*ItemFromHook(curr));
      curr = curr->next.load(std::memory_order_acquire);
    }
  }

  std::size_t CountUnsafe() const {
    std::size_t count = 0;
    ForEach([&count](const T&) { ++count; });
    return count;
  }

  // --- Retire functions ----------------------------------------------------

  // Remove item and defer deletion via RcuManager::CallAfterGracePeriod.
  // Use when the list is owned by the control-plane thread.
  void RemoveAndRetireGracePeriod(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;

    auto status = rcu_manager_->CallAfterGracePeriod(
        [item, fn = std::move(retire_fn)]() { fn(item); });
    assert(status.ok());
    (void)status;
  }

  // Remove item and defer deletion via RcuManager::PostDeferredWork.
  // Use when the list is owned by a PMD thread (without a PmdRetireState).
  void RemoveAndRetireDeferred(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;

    struct rte_rcu_qsbr* qsbr = rcu_manager_->GetQsbrVar();
    assert(qsbr != nullptr);

    auto* work = new DeferredWorkItem();
    work->token = rte_rcu_qsbr_start(qsbr);
    work->callback = [item, fn = std::move(retire_fn)]() { fn(item); };
    rcu_manager_->PostDeferredWork(work);
  }

  // Remove item and defer deletion via the shared PmdRetireState.
  // The caller must call pmd_retire_state->RefreshScheduling() each PMD
  // iteration (same pattern as RefreshGcScheduling in
  // FiveTupleForwardingProcessor).
  void RemoveAndRetirePmdJob(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ != nullptr);
    assert(item != nullptr && retire_fn);
    bool removed = Remove(item);
    assert(removed);
    (void)removed;

    pmd_retire_state_->AddPendingRetire(
        [item, fn = std::move(retire_fn)]() { fn(item); });
  }

 private:
  static IntrusiveRcuListHook* Hook(T* item) { return &(item->*HookMember); }

  // container_of: recover the owning T* from a hook pointer using the
  // compile-time member offset (same pattern as boost::intrusive and the
  // Linux kernel's container_of macro).
  static T* ItemFromHook(const IntrusiveRcuListHook* hook) {
    return reinterpret_cast<T*>(
        reinterpret_cast<uintptr_t>(hook) -
        reinterpret_cast<uintptr_t>(
            &(static_cast<const T*>(nullptr)->*HookMember)));
  }

  std::atomic<IntrusiveRcuListHook*> head_{nullptr};
  RcuManager* rcu_manager_ = nullptr;
  PmdRetireState* pmd_retire_state_ = nullptr;  // non-owning
};

}  // namespace rcu

#endif  // RCU_INTRUSIVE_RCU_LIST_H_
