#ifndef RCU_INTRUSIVE_RCU_LIST_H_
#define RCU_INTRUSIVE_RCU_LIST_H_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace rcu {

struct IntrusiveRcuListHook {
  std::atomic<IntrusiveRcuListHook*> next{nullptr};
};

// ---------------------------------------------------------------------------
// IntrusiveRcuList — lightweight intrusive singly-linked list.
//
// The list itself owns only a head pointer.  It provides insert, remove,
// traversal, and lookup — nothing more.  Reclamation of removed items is
// the caller's responsibility; see rcu_retire.h for free helper functions
// (RetireViaGracePeriod, RetireViaDeferred, RetireViaPmdJob).
//
// Writers (InsertHead / Remove) must be serialized externally.
// ForEach / FindIf are safe for concurrent read-only traversal.
// ---------------------------------------------------------------------------
template <typename T, IntrusiveRcuListHook T::*HookMember>
class IntrusiveRcuList {
 public:
  IntrusiveRcuList() = default;
  IntrusiveRcuList(const IntrusiveRcuList&) = delete;
  IntrusiveRcuList& operator=(const IntrusiveRcuList&) = delete;

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

  // --- List operations -----------------------------------------------------

  // Single-writer prepend.
  bool InsertHead(T* item) {
    if (item == nullptr) return false;
    IntrusiveRcuListHook* hook = Hook(item);
    hook->next.store(head_.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    head_.store(hook, std::memory_order_release);
    return true;
  }

  // Unlink item from the list.  Returns true if found and removed.
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

  // RCU-safe traversal.  fn receives T& (non-const) or const T&.
  template <typename Fn>
  void ForEach(Fn&& fn) const {
    IntrusiveRcuListHook* curr = head_.load(std::memory_order_acquire);
    while (curr != nullptr) {
      fn(*ItemFromHook(curr));
      curr = curr->next.load(std::memory_order_acquire);
    }
  }

  // Return the first node matching the predicate, or nullptr.
  template <typename Pred>
  T* FindIf(Pred&& pred) const {
    IntrusiveRcuListHook* curr = head_.load(std::memory_order_acquire);
    while (curr != nullptr) {
      T* item = ItemFromHook(curr);
      if (pred(*item)) return item;
      curr = curr->next.load(std::memory_order_acquire);
    }
    return nullptr;
  }

  std::size_t CountUnsafe() const {
    std::size_t count = 0;
    ForEach([&count](const T&) { ++count; });
    return count;
  }

  bool Empty() const {
    return head_.load(std::memory_order_relaxed) == nullptr;
  }

 private:
  std::atomic<IntrusiveRcuListHook*> head_{nullptr};
};

}  // namespace rcu

#endif  // RCU_INTRUSIVE_RCU_LIST_H_
