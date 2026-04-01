#include "rcu/intrusive_rcu_list.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "processor/pmd_job.h"
#include "rxtx/test_utils.h"

namespace rcu {
namespace {

struct TestNode {
  explicit TestNode(int value_in) : value(value_in) {}
  int value = 0;
  IntrusiveRcuListHook hook;
};

using TestList = IntrusiveRcuList<TestNode, &TestNode::hook>;

// --- Pure list operations (no DPDK, no retire) ---

TEST(IntrusiveRcuListTest, InsertRemoveAndTraverse) {
  TestList list;
  TestNode a(1);
  TestNode b(2);
  TestNode c(3);

  EXPECT_TRUE(list.InsertHead(&a));
  EXPECT_TRUE(list.InsertHead(&b));
  EXPECT_TRUE(list.InsertHead(&c));
  EXPECT_EQ(list.CountUnsafe(), 3u);

  std::vector<int> values;
  list.ForEach([&values](const TestNode& n) { values.push_back(n.value); });
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 3);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 1);

  EXPECT_TRUE(list.Remove(&b));
  EXPECT_EQ(list.CountUnsafe(), 2u);
  EXPECT_FALSE(list.Remove(&b));
}

TEST(IntrusiveRcuListTest, InsertNull) {
  TestList list;
  EXPECT_FALSE(list.InsertHead(nullptr));
  EXPECT_EQ(list.CountUnsafe(), 0u);
}

TEST(IntrusiveRcuListTest, RemoveHead) {
  TestList list;
  TestNode a(1);
  TestNode b(2);
  list.InsertHead(&a);
  list.InsertHead(&b);

  EXPECT_TRUE(list.Remove(&b));
  EXPECT_EQ(list.CountUnsafe(), 1u);

  std::vector<int> values;
  list.ForEach([&values](const TestNode& n) { values.push_back(n.value); });
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 1);
}

TEST(IntrusiveRcuListTest, RemoveTail) {
  TestList list;
  TestNode a(1);
  TestNode b(2);
  list.InsertHead(&a);
  list.InsertHead(&b);

  EXPECT_TRUE(list.Remove(&a));
  EXPECT_EQ(list.CountUnsafe(), 1u);

  std::vector<int> values;
  list.ForEach([&values](const TestNode& n) { values.push_back(n.value); });
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 2);
}

// --- Retire tests (require DPDK EAL + RcuManager) ---

class IntrusiveRcuListRetireTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
    RcuManager::Config cfg;
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
  RcuManager rcu_manager_;
};

TEST_F(IntrusiveRcuListRetireTest, RetireViaCallAfterGracePeriod) {
  TestList list(&rcu_manager_);
  auto node = std::make_unique<TestNode>(10);
  bool retired = false;

  list.InsertHead(node.get());
  TestNode* raw = node.release();
  list.RemoveAndRetireGracePeriod(raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_EQ(list.CountUnsafe(), 0u);
  EXPECT_FALSE(retired);

  PollIoUntil(true, &retired);
  EXPECT_TRUE(retired);
}

TEST_F(IntrusiveRcuListRetireTest, RetireViaPostDeferredWork) {
  TestList list(&rcu_manager_);
  auto node = std::make_unique<TestNode>(20);
  bool retired = false;

  list.InsertHead(node.get());
  TestNode* raw = node.release();
  list.RemoveAndRetireDeferred(raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_EQ(list.CountUnsafe(), 0u);
  EXPECT_FALSE(retired);

  PollIoUntil(true, &retired);
  EXPECT_TRUE(retired);
}

TEST_F(IntrusiveRcuListRetireTest, RetireViaPmdJobRunner) {
  processor::PmdJobRunner runner;
  PmdRetireState pmd_retire_state(&rcu_manager_, &runner);
  TestList list(&rcu_manager_, &pmd_retire_state);
  auto node = std::make_unique<TestNode>(30);
  bool retired = false;

  list.InsertHead(node.get());
  TestNode* raw = node.release();
  list.RemoveAndRetirePmdJob(raw, [&retired](TestNode* n) {
    retired = true;
    delete n;
  });
  EXPECT_EQ(list.CountUnsafe(), 0u);
  EXPECT_FALSE(retired);
  EXPECT_TRUE(pmd_retire_state.HasPending());

  // Simulate the PMD loop: refresh scheduling → run jobs → advance QSBR.
  for (int i = 0; i < 200 && !retired; ++i) {
    pmd_retire_state.RefreshScheduling();
    runner.RunRunnableJobs(0);
    ProgressGracePeriod();
  }
  EXPECT_TRUE(retired);
  EXPECT_FALSE(pmd_retire_state.HasPending());
}

}  // namespace
}  // namespace rcu
