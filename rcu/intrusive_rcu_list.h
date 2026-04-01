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
//
// --- Static API ---
// Static chain operations (ChainInsert, ChainRemove, ChainForEach,
// ChainFindIf) operate on any external atomic<Hook*> head pointer.
// Static retire helpers (RetireGracePeriod, RetireDeferred, RetirePmdJob)
// schedule reclamation without touching the chain.  Both IntrusiveRcuList
// and RcuHashTable build on these statics.
// ---------------------------------------------------------------------------
template <typename T, IntrusiveRcuListHook T::*HookMember>
class IntrusiveRcuList {
 public:
  // --- Static hook helpers (public) ----------------------------------------

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

  // --- Static chain operations ---------------------------------------------
  // Operate on any external atomic<Hook*> head.  These are the building
  // blocks shared by IntrusiveRcuList (instance methods) and RcuHashTable.

  // Single-writer prepend.
  static bool ChainInsert(std::atomic<IntrusiveRcuListHook*>& head, T* item) {
    if (item == nullptr) return false;
    IntrusiveRcuListHook* hook = Hook(item);
    hook->next.store(head.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    head.store(hook, std::memory_order_release);
    return true;
  }

  // Unlink item from the chain.  Returns true if found and removed.
  static bool ChainRemove(std::atomic<IntrusiveRcuListHook*>& head, T* item) {
    if (item == nullptr) return false;
    IntrusiveRcuListHook* target = Hook(item);
    IntrusiveRcuListHook* prev = nullptr;
    IntrusiveRcuListHook* curr = head.load(std::memory_order_acquire);
    while (curr != nullptr) {
      IntrusiveRcuListHook* next = curr->next.load(std::memory_order_acquire);
      if (curr == target) {
        if (prev == nullptr) {
          head.store(next, std::memory_order_release);
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

  // RCU-safe traversal.  fn receives T& (non-const) or const T&.
  template <typename Fn>
  static void ChainForEach(const std::atomic<IntrusiveRcuListHook*>& head,
                           Fn&& fn) {
    IntrusiveRcuListHook* curr = head.load(std::memory_order_acquire);
    while (curr != nullptr) {
      fn(*ItemFromHook(curr));
      curr = curr->next.load(std::memory_order_acquire);
    }
  }

  // Return the first node matching the predicate, or nullptr.
  template <typename Pred>
  static T* ChainFindIf(const std::atomic<IntrusiveRcuListHook*>& head,
                         Pred&& pred) {
    IntrusiveRcuListHook* curr = head.load(std::memory_order_acquire);
    while (curr != nullptr) {
      T* item = ItemFromHook(curr);
      if (pred(*item)) return item;
      curr = curr->next.load(std::memory_order_acquire);
    }
    return nullptr;
  }

  // --- Static retire helpers -----------------------------------------------
  // Schedule reclamation for an already-removed item.  These do NOT touch the
  // chain — the caller is responsible for unlinking first.

  static void RetireGracePeriod(RcuManager* mgr, T* item,
                                RetireFn<T> retire_fn) {
    assert(mgr != nullptr && item != nullptr && retire_fn);
    auto status = mgr->CallAfterGracePeriod(
        [item, fn = std::move(retire_fn)]() { fn(item); });
    assert(status.ok());
    (void)status;
  }

  static void RetireDeferred(RcuManager* mgr, T* item,
                             RetireFn<T> retire_fn) {
    assert(mgr != nullptr && item != nullptr && retire_fn);
    struct rte_rcu_qsbr* qsbr = mgr->GetQsbrVar();
    assert(qsbr != nullptr);
    auto* work = new DeferredWorkItem();
    work->token = rte_rcu_qsbr_start(qsbr);
    work->callback = [item, fn = std::move(retire_fn)]() { fn(item); };
    mgr->PostDeferredWork(work);
  }

  static void RetirePmdJob(PmdRetireState* state, T* item,
                           RetireFn<T> retire_fn) {
    assert(state != nullptr && item != nullptr && retire_fn);
    state->AddPendingRetire(
        [item, fn = std::move(retire_fn)]() { fn(item); });
  }

  // --- Constructors --------------------------------------------------------

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

  // --- Instance list operations (delegate to static chain ops) -------------

  bool InsertHead(T* item) { return ChainInsert(head_, item); }
  bool Remove(T* item) { return ChainRemove(head_, item); }

  template <typename Fn>
  void ForEach(Fn&& fn) const { ChainForEach(head_, std::forward<Fn>(fn)); }

  template <typename Pred>
  T* FindIf(Pred&& pred) const {
    return ChainFindIf(head_, std::forward<Pred>(pred));
  }

  std::size_t CountUnsafe() const {
    std::size_t count = 0;
    ForEach([&count](const T&) { ++count; });
    return count;
  }

  // --- Instance retire functions (remove + static retire) ------------------

  void RemoveAndRetireGracePeriod(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    bool removed = ChainRemove(head_, item);
    assert(removed);
    (void)removed;
    RetireGracePeriod(rcu_manager_, item, std::move(retire_fn));
  }

  void RemoveAndRetireDeferred(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ == nullptr);
    bool removed = ChainRemove(head_, item);
    assert(removed);
    (void)removed;
    RetireDeferred(rcu_manager_, item, std::move(retire_fn));
  }

  void RemoveAndRetirePmdJob(T* item, RetireFn<T> retire_fn) {
    assert(rcu_manager_ != nullptr && pmd_retire_state_ != nullptr);
    bool removed = ChainRemove(head_, item);
    assert(removed);
    (void)removed;
    RetirePmdJob(pmd_retire_state_, item, std::move(retire_fn));
  }

 private:
  std::atomic<IntrusiveRcuListHook*> head_{nullptr};
  RcuManager* rcu_manager_ = nullptr;
  PmdRetireState* pmd_retire_state_ = nullptr;  // non-owning
};

}  // namespace rcu

#endif  // RCU_INTRUSIVE_RCU_LIST_H_
