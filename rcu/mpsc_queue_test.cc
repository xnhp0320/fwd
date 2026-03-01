// Feature: dpdk-rcu-async, Property 1: MPSC Queue Preserves All Items Under
// Concurrent Push

#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "rcu/mpsc_queue.h"

namespace rcu {
namespace {

// **Validates: Requirements 9.7, 10.6**
//
// For any set of N DeferredWorkItems pushed concurrently by M producer threads
// into the MPSC queue, popping from the single consumer thread should yield
// exactly N items, with no items lost or duplicated.
RC_GTEST_PROP(MpscQueueProperty,
              PreservesAllItemsUnderConcurrentPush,
              ()) {
  const auto num_threads =
      *rc::gen::inRange(1, 9);  // M ∈ [1, 8]
  const auto items_per_thread =
      *rc::gen::inRange(1, 1001);  // K ∈ [1, 1000]

  MpscQueue queue;

  // Each thread gets a unique ID range: thread i pushes tokens
  // [i * items_per_thread, (i+1) * items_per_thread).
  // This gives every item a globally unique token.
  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  // Heap-allocate all items upfront so we can track them.
  // Each thread owns a contiguous slice of the items vector.
  std::vector<std::vector<DeferredWorkItem*>> per_thread_items(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    per_thread_items[t].reserve(items_per_thread);
    for (int k = 0; k < items_per_thread; ++k) {
      auto* item = new DeferredWorkItem;
      item->token = static_cast<uint64_t>(t) * items_per_thread + k;
      per_thread_items[t].push_back(item);
    }
  }

  // Spawn M producer threads, each pushing K items.
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&queue, &per_thread_items, t]() {
      for (auto* item : per_thread_items[t]) {
        queue.Push(item);
      }
    });
  }

  // Wait for all producers to finish.
  for (auto& th : threads) {
    th.join();
  }

  // Single consumer pops all items.
  std::set<uint64_t> seen_tokens;
  int pop_count = 0;

  while (true) {
    DeferredWorkItem* item = queue.Pop();
    if (item == nullptr) {
      // The Vyukov queue can return nullptr transiently when the last
      // producer's next pointer hasn't been linked yet. Try once more
      // after a brief pause if we haven't collected everything.
      if (pop_count < num_threads * items_per_thread) {
        item = queue.Pop();
      }
      if (item == nullptr) {
        break;
      }
    }
    seen_tokens.insert(item->token);
    ++pop_count;
    delete item;
  }

  const int expected_total = num_threads * items_per_thread;

  // Verify total count: no items lost.
  RC_ASSERT(pop_count == expected_total);

  // Verify no duplicates: set size must equal total count.
  RC_ASSERT(static_cast<int>(seen_tokens.size()) == expected_total);
}

// ---------------------------------------------------------------------------
// Unit tests for MpscQueue
// Validates: Requirements 9.2, 9.5
// ---------------------------------------------------------------------------

TEST(MpscQueueTest, EmptyPopReturnsNullptr) {
  MpscQueue queue;
  EXPECT_EQ(queue.Pop(), nullptr);
}

TEST(MpscQueueTest, SinglePushPopReturnsCorrectItem) {
  MpscQueue queue;
  DeferredWorkItem item;
  item.token = 42;

  queue.Push(&item);
  DeferredWorkItem* popped = queue.Pop();

  ASSERT_NE(popped, nullptr);
  EXPECT_EQ(popped->token, 42u);
  EXPECT_EQ(popped, &item);

  // Next pop should return nullptr (queue is now empty).
  EXPECT_EQ(queue.Pop(), nullptr);
}

TEST(MpscQueueTest, PushPushPopPopPreservesFifoOrder) {
  MpscQueue queue;
  DeferredWorkItem a;
  a.token = 1;
  DeferredWorkItem b;
  b.token = 2;

  queue.Push(&a);
  queue.Push(&b);

  DeferredWorkItem* first = queue.Pop();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->token, 1u);

  DeferredWorkItem* second = queue.Pop();
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->token, 2u);

  EXPECT_EQ(queue.Pop(), nullptr);
}

TEST(MpscQueueTest, StubReinsertionEdgeCase) {
  // Push a single item. The first Pop skips the stub and sees the item as
  // the only node. Because its next is nullptr and it equals head_, Pop
  // re-inserts the stub to unlink it. This exercises the stub re-insertion
  // path in the Vyukov algorithm.
  MpscQueue queue;
  DeferredWorkItem item;
  item.token = 99;

  queue.Push(&item);

  DeferredWorkItem* popped = queue.Pop();
  ASSERT_NE(popped, nullptr);
  EXPECT_EQ(popped->token, 99u);

  // After the stub re-insertion the queue should be logically empty.
  EXPECT_EQ(queue.Pop(), nullptr);

  // The queue must still be usable after the stub re-insertion.
  DeferredWorkItem another;
  another.token = 100;
  queue.Push(&another);

  DeferredWorkItem* popped2 = queue.Pop();
  ASSERT_NE(popped2, nullptr);
  EXPECT_EQ(popped2->token, 100u);
  EXPECT_EQ(queue.Pop(), nullptr);
}

}  // namespace
}  // namespace rcu
