#ifndef RCU_MPSC_QUEUE_H_
#define RCU_MPSC_QUEUE_H_

#include <atomic>

#include "rcu/deferred_work_item.h"

namespace rcu {

// Lock-free intrusive MPSC (Multiple Producer, Single Consumer) queue based on
// Dmitry Vyukov's algorithm (1024cores.net).
//
// Producers enqueue with a single atomic exchange (wait-free).
// The single consumer dequeues in a lock-free manner.
//
// The queue uses a sentinel (stub) node that is always present. head_ is an
// atomic pointer that producers XCHG to enqueue. tail_ is a plain pointer
// used only by the consumer.
//
// Thread safety:
//   Push() — safe to call from any thread concurrently (wait-free).
//   Pop()  — must be called from a single consumer thread only.
//   Empty() — approximate; may return false negatives during concurrent Push.
class MpscQueue {
 public:
  MpscQueue() {
    stub_.next.store(nullptr, std::memory_order_relaxed);
    head_.store(&stub_, std::memory_order_relaxed);
    tail_ = &stub_;
  }

  // Non-copyable, non-movable (contains self-referential pointers).
  MpscQueue(const MpscQueue&) = delete;
  MpscQueue& operator=(const MpscQueue&) = delete;

  // Push a node into the queue. Wait-free. Safe to call from any thread.
  void Push(DeferredWorkItem* node) {
    node->next.store(nullptr, std::memory_order_relaxed);
    DeferredWorkItem* prev =
        head_.exchange(node, std::memory_order_acq_rel);
    prev->next.store(node, std::memory_order_release);
  }

  // Pop a node from the queue. Lock-free. Must be called from a single
  // consumer thread only (the control plane).
  // Returns nullptr if the queue is empty or temporarily inconsistent.
  DeferredWorkItem* Pop() {
    DeferredWorkItem* tail = tail_;
    DeferredWorkItem* next = tail->next.load(std::memory_order_acquire);

    if (tail == &stub_) {
      if (next == nullptr) {
        return nullptr;  // Queue is empty.
      }
      tail_ = next;  // Skip past stub.
      tail = next;
      next = next->next.load(std::memory_order_acquire);
    }

    if (next != nullptr) {
      tail_ = next;
      return tail;
    }

    // Only one node left — it might be the last producer's node whose next
    // pointer hasn't been linked yet.
    DeferredWorkItem* head_node = head_.load(std::memory_order_acquire);
    if (tail != head_node) {
      return nullptr;  // Producer hasn't finished linking yet.
    }

    // Re-insert stub to allow the last node to be dequeued.
    Push(&stub_);

    next = tail->next.load(std::memory_order_acquire);
    if (next != nullptr) {
      tail_ = next;
      return tail;
    }

    return nullptr;
  }

  // Check if the queue appears empty. Approximate — may return false
  // negatives during concurrent Push operations.
  bool Empty() const {
    return tail_ == &stub_ &&
           stub_.next.load(std::memory_order_acquire) == nullptr;
  }

 private:
  // Sentinel node. Always present in the queue.
  DeferredWorkItem stub_;

  // Head pointer. Producers XCHG here to enqueue.
  std::atomic<DeferredWorkItem*> head_;

  // Tail pointer. Consumer-only. Points to the next node to dequeue.
  DeferredWorkItem* tail_;
};

}  // namespace rcu

#endif  // RCU_MPSC_QUEUE_H_
