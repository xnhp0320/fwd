#include "rxtx/rcu_hash_table.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "processor/pmd_job.h"
#include "rcu/rcu_retire.h"
#include "rxtx/test_utils.h"

namespace rxtx {
namespace {

// --- Test node and helpers -------------------------------------------------

struct TestNode {
  explicit TestNode(int key_in, int value_in = 0)
      : key(key_in), value(value_in) {}
  int key = 0;
  int value = 0;
  rcu::IntrusiveRcuListHook hook;
};

struct TestKeyExtractor {
  const int& operator()(const TestNode& node) const { return node.key; }
};

// Fibonacci-style hash for well-distributed bits across bucket and sig space.
struct TestHash {
  std::size_t operator()(int key) const {
    return static_cast<std::size_t>(key) * 0x9E3779B97F4A7C15ULL;
  }
};

using TestTable = RcuHashTable<TestNode, &TestNode::hook, int,
                               TestKeyExtractor, TestHash>;

// --- Pure table operations (no DPDK, no retire) ----------------------------

TEST(RcuHashTableTest, InsertAndFind) {
  ASSERT_TRUE(rxtx::testing::InitEal());
  TestTable table(16);

  TestNode a(1, 10);
  TestNode b(2, 20);
  TestNode c(3, 30);

  EXPECT_TRUE(table.Insert(&a));
  EXPECT_TRUE(table.Insert(&b));
  EXPECT_TRUE(table.Insert(&c));

  EXPECT_EQ(table.Find(1), &a);
  EXPECT_EQ(table.Find(2), &b);
  EXPECT_EQ(table.Find(3), &c);
  EXPECT_EQ(table.Find(999), nullptr);
}

TEST(RcuHashTableTest, PrefetchAndFindWithPrefetch) {
  ASSERT_TRUE(rxtx::testing::InitEal());
  TestTable table(16);

  TestNode a(7, 70);
  table.Insert(&a);

  TestTable::PrefetchContext ctx{};
  table.Prefetch(7, ctx);
  EXPECT_EQ(table.FindWithPrefetch(7, ctx), &a);
  EXPECT_EQ(table.FindWithPrefetch(8, ctx), nullptr);
}

TEST(RcuHashTableTest, InsertDuplicate) {
  ASSERT_TRUE(rxtx::testing::InitEal());
  TestTable table(16);

  TestNode a(1, 10);
  TestNode a2(1, 99);

  EXPECT_TRUE(table.Insert(&a));
  EXPECT_FALSE(table.Insert(&a2));
  EXPECT_EQ(table.Find(1)->value, 10);
}

TEST(RcuHashTableTest, RemoveAndFind) {
  ASSERT_TRUE(rxtx::testing::InitEal());
  TestTable table(16);

  TestNode a(1, 10);
  TestNode b(2, 20);

  table.Insert(&a);
  table.Insert(&b);

  EXPECT_TRUE(table.Remove(&a));
  EXPECT_EQ(table.Find(1), nullptr);
  EXPECT_EQ(table.Find(2), &b);

  EXPECT_FALSE(table.Remove(&a));
}

TEST(RcuHashTableTest, ForEachVisitsAll) {
  ASSERT_TRUE(rxtx::testing::InitEal());
  TestTable table(16);

  TestNode a(1, 10);
  TestNode b(2, 20);
  TestNode c(3, 30);

  table.Insert(&a);
  table.Insert(&b);
  table.Insert(&c);

  int sum = 0;
  int count = 0;
  table.ForEach([&](const TestNode& node) {
    sum += node.value;
    ++count;
  });

  EXPECT_EQ(count, 3);
  EXPECT_EQ(sum, 60);
}

TEST(RcuHashTableTest, OverflowChain) {
  ASSERT_TRUE(rxtx::testing::InitEal());

  // Use 1 bucket so all keys hash to the same bucket.
  TestTable table(1);

  // Insert 5 nodes — the first 4 should fill sig slots (assuming distinct
  // sigs from TestHash), the 5th should go to the overflow chain.
  TestNode nodes[6] = {
      TestNode(10, 100), TestNode(20, 200), TestNode(30, 300),
      TestNode(40, 400), TestNode(50, 500), TestNode(60, 600),
  };

  for (auto& n : nodes) {
    EXPECT_TRUE(table.Insert(&n));
  }

  // All should be findable.
  for (auto& n : nodes) {
    EXPECT_EQ(table.Find(n.key), &n);
  }

  // Remove one and verify the rest still work.
  EXPECT_TRUE(table.Remove(&nodes[2]));
  EXPECT_EQ(table.Find(30), nullptr);
  for (int i = 0; i < 6; ++i) {
    if (i == 2) continue;
    EXPECT_EQ(table.Find(nodes[i].key), &nodes[i]);
  }
}

TEST(RcuHashTableTest, SameSigChaining) {
  ASSERT_TRUE(rxtx::testing::InitEal());

  // Custom hash that forces all keys to produce the same finalized hash,
  // meaning same bucket AND same sig.  All nodes chain under one sig slot.
  struct ConstHash {
    std::size_t operator()(int) const {
      return 0x0001000000000000ULL;  // sig = 0x0001 after finalize
    }
  };

  using ConstTable = RcuHashTable<TestNode, &TestNode::hook, int,
                                  TestKeyExtractor, ConstHash>;
  ConstTable table(1);

  TestNode a(1, 10);
  TestNode b(2, 20);
  TestNode c(3, 30);

  EXPECT_TRUE(table.Insert(&a));
  EXPECT_TRUE(table.Insert(&b));
  EXPECT_TRUE(table.Insert(&c));

  EXPECT_EQ(table.Find(1), &a);
  EXPECT_EQ(table.Find(2), &b);
  EXPECT_EQ(table.Find(3), &c);

  EXPECT_TRUE(table.Remove(&b));
  EXPECT_EQ(table.Find(2), nullptr);
  EXPECT_EQ(table.Find(1), &a);
  EXPECT_EQ(table.Find(3), &c);
}

// --- Retire tests (require DPDK EAL + RcuManager) -------------------------

class RcuHashTableRetireTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
    rcu::RcuManager::Config cfg;
    cfg.max_threads = 8;
    cfg.poll_interval_ms = 1;
    ASSERT_TRUE(rcu_manager_.Init(io_ctx_, cfg).ok());
    ASSERT_TRUE(rcu_manager_.RegisterThread(0).ok());
    ASSERT_TRUE(rcu_manager_.Start().ok());
  }

  void TearDown() override {
    rcu_manager_.Stop();
    (void)rcu_manager_.UnregisterThread(0);
  }

  void ProgressGracePeriod() {
    rte_rcu_qsbr_quiescent(rcu_manager_.GetQsbrVar(), 0);
  }

  void PollIoUntil(bool expected, bool* flag) {
    for (int i = 0; i < 200 && (*flag != expected); ++i) {
      ProgressGracePeriod();
      io_ctx_.restart();
      io_ctx_.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  boost::asio::io_context io_ctx_;
  rcu::RcuManager rcu_manager_;
};

TEST_F(RcuHashTableRetireTest, RetireViaCallAfterGracePeriod) {
  TestTable table(16);
  auto node = std::make_unique<TestNode>(42, 99);
  bool retired = false;

  table.Insert(node.get());
  TestNode* raw = node.release();
  EXPECT_TRUE(table.Remove(raw));
  EXPECT_EQ(table.Find(42), nullptr);

  rcu::RetireViaGracePeriod(&rcu_manager_, raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_FALSE(retired);

  PollIoUntil(true, &retired);
  EXPECT_TRUE(retired);
}

TEST_F(RcuHashTableRetireTest, RetireViaPostDeferredWork) {
  TestTable table(16);
  auto node = std::make_unique<TestNode>(42, 99);
  bool retired = false;

  table.Insert(node.get());
  TestNode* raw = node.release();
  EXPECT_TRUE(table.Remove(raw));
  EXPECT_EQ(table.Find(42), nullptr);

  rcu::RetireViaDeferred(&rcu_manager_, raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_FALSE(retired);

  PollIoUntil(true, &retired);
  EXPECT_TRUE(retired);
}

TEST_F(RcuHashTableRetireTest, RetireViaPmdJobRunner) {
  processor::PmdJobRunner runner;
  rcu::PmdRetireState pmd_retire_state(&rcu_manager_, &runner);
  TestTable table(16);
  auto node = std::make_unique<TestNode>(42, 99);
  bool retired = false;

  table.Insert(node.get());
  TestNode* raw = node.release();
  EXPECT_TRUE(table.Remove(raw));
  EXPECT_EQ(table.Find(42), nullptr);

  rcu::RetireViaPmdJob(&pmd_retire_state, raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_FALSE(retired);
  EXPECT_TRUE(pmd_retire_state.HasPending());

  for (int i = 0; i < 200 && !retired; ++i) {
    pmd_retire_state.RefreshScheduling();
    runner.RunRunnableJobs(0);
    ProgressGracePeriod();
  }
  EXPECT_TRUE(retired);
  EXPECT_FALSE(pmd_retire_state.HasPending());
}

}  // namespace
}  // namespace rxtx
