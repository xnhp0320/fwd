#include "indirect_table/indirect_table.h"

#include <cstdint>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rte_rcu_qsbr.h>

#include "boost/asio/io_context.hpp"
#include "rcu/rcu_manager.h"
#include "rxtx/test_utils.h"

namespace indirect_table {
namespace {

using TestTable = IndirectTable<uint32_t, uint32_t>;
using SlotArr = TestTable::SlotArrayType;

class IndirectTableTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }

  void SetUp() override {
    rcu::RcuManager::Config rcu_cfg;
    rcu_cfg.max_threads = 8;
    rcu_cfg.poll_interval_ms = 1;
    ASSERT_TRUE(rcu_manager_.Init(io_ctx_, rcu_cfg).ok());
    ASSERT_TRUE(rcu_manager_.RegisterThread(0).ok());
    ASSERT_TRUE(rcu_manager_.Start().ok());

    TestTable::Config cfg;
    cfg.value_capacity = 16;
    cfg.value_bucket_count = 16;
    cfg.key_capacity = 31;  // rte_mempool requires (2^n - 1)
    cfg.key_bucket_count = 16;
    cfg.name = "test_it";
    ASSERT_TRUE(table_.Init(cfg, &rcu_manager_).ok());
  }

  void TearDown() override {
    rcu_manager_.Stop();
    (void)rcu_manager_.UnregisterThread(0);
  }

  // Signal a quiescent state and poll the io_context to process pending
  // RCU grace period callbacks.
  void ProgressGracePeriod() {
    rte_rcu_qsbr_quiescent(rcu_manager_.GetQsbrVar(), 0);
  }

  void PollIoUntilDone(bool expected, bool* flag) {
    for (int i = 0; i < 200 && (*flag != expected); ++i) {
      ProgressGracePeriod();
      io_ctx_.restart();
      io_ctx_.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  boost::asio::io_context io_ctx_;
  rcu::RcuManager rcu_manager_;
  TestTable table_;
};

// ---------- 1. Init succeeds and table is ready for operations ---------------

TEST_F(IndirectTableTest, InitSucceeds) {
  // After SetUp, the table should be usable.
  EXPECT_EQ(table_.slot_array().used_count(), 0u);
  EXPECT_EQ(table_.slot_array().capacity(), 16u);
  EXPECT_EQ(table_.Find(42), nullptr);
}

// ---------- 2. Insert/Find round-trip, duplicate key returns kInvalidId ------

TEST_F(IndirectTableTest, InsertFindRoundTrip) {
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);

  auto* entry = table_.Find(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->key, 1u);
  EXPECT_EQ(entry->value_id, vid);
  EXPECT_EQ(*table_.slot_array().Get(vid), 100u);
}

TEST_F(IndirectTableTest, DuplicateKeyReturnsInvalidId) {
  uint32_t vid1 = table_.Insert(1, 100);
  ASSERT_NE(vid1, SlotArr::kInvalidId);

  // Same key, different value — should be rejected.
  uint32_t vid2 = table_.Insert(1, 200);
  EXPECT_EQ(vid2, SlotArr::kInvalidId);

  // Original entry unchanged.
  EXPECT_EQ(*table_.slot_array().Get(vid1), 100u);
}

// ---------- 3. Many-to-one: multiple keys with same value share one slot -----

TEST_F(IndirectTableTest, ManyToOneDedup) {
  uint32_t vid1 = table_.Insert(1, 42);
  uint32_t vid2 = table_.Insert(2, 42);
  uint32_t vid3 = table_.Insert(3, 42);

  ASSERT_NE(vid1, SlotArr::kInvalidId);
  ASSERT_NE(vid2, SlotArr::kInvalidId);
  ASSERT_NE(vid3, SlotArr::kInvalidId);

  // All three keys should share the same value slot.
  EXPECT_EQ(vid1, vid2);
  EXPECT_EQ(vid2, vid3);

  // Only one slot used.
  EXPECT_EQ(table_.slot_array().used_count(), 1u);
  EXPECT_EQ(table_.slot_array().RefCount(vid1), 3u);
}

// ---------- 4. InsertWithId attaches key to existing slot --------------------

TEST_F(IndirectTableTest, InsertWithIdAttachesKey) {
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);
  EXPECT_EQ(table_.slot_array().RefCount(vid), 1u);

  // Attach a second key to the same slot.
  EXPECT_TRUE(table_.InsertWithId(2, vid));
  EXPECT_EQ(table_.slot_array().RefCount(vid), 2u);

  // Both keys should resolve to the same value.
  auto* e1 = table_.Find(1);
  auto* e2 = table_.Find(2);
  ASSERT_NE(e1, nullptr);
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(e1->value_id, vid);
  EXPECT_EQ(e2->value_id, vid);
}

TEST_F(IndirectTableTest, InsertWithIdDuplicateKeyReturnsFalse) {
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);

  // Duplicate key should fail.
  EXPECT_FALSE(table_.InsertWithId(1, vid));
  EXPECT_EQ(table_.slot_array().RefCount(vid), 1u);
}

// ---------- 5. Remove returns true/false correctly ---------------------------

TEST_F(IndirectTableTest, RemoveExistingKeyReturnsTrue) {
  table_.Insert(1, 100);
  EXPECT_TRUE(table_.Remove(1));
  EXPECT_EQ(table_.Find(1), nullptr);
}

TEST_F(IndirectTableTest, RemoveNonExistentKeyReturnsFalse) {
  EXPECT_FALSE(table_.Remove(999));
}

// ---------- 6. Mempool exhaustion rollback -----------------------------------

class IndirectTableSmallPoolTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }

  void SetUp() override {
    rcu::RcuManager::Config rcu_cfg;
    rcu_cfg.max_threads = 8;
    rcu_cfg.poll_interval_ms = 1;
    ASSERT_TRUE(rcu_manager_.Init(io_ctx_, rcu_cfg).ok());
    ASSERT_TRUE(rcu_manager_.RegisterThread(0).ok());
    ASSERT_TRUE(rcu_manager_.Start().ok());

    TestTable::Config cfg;
    cfg.value_capacity = 16;
    cfg.value_bucket_count = 16;
    cfg.key_capacity = 3;  // Very small: only 3 KeyEntries
    cfg.key_bucket_count = 4;
    cfg.name = "test_it_small";
    ASSERT_TRUE(table_.Init(cfg, &rcu_manager_).ok());
  }

  void TearDown() override {
    rcu_manager_.Stop();
    (void)rcu_manager_.UnregisterThread(0);
  }

  boost::asio::io_context io_ctx_;
  rcu::RcuManager rcu_manager_;
  TestTable table_;
};

TEST_F(IndirectTableSmallPoolTest, InsertRollbackOnMempoolExhaustion) {
  // Fill the mempool (capacity=3).
  ASSERT_NE(table_.Insert(1, 100), SlotArr::kInvalidId);
  ASSERT_NE(table_.Insert(2, 200), SlotArr::kInvalidId);
  ASSERT_NE(table_.Insert(3, 300), SlotArr::kInvalidId);

  uint32_t used_before = table_.slot_array().used_count();

  // 4th insert should fail due to mempool exhaustion.
  uint32_t vid = table_.Insert(4, 400);
  EXPECT_EQ(vid, SlotArr::kInvalidId);

  // Slot array should be rolled back — no extra slot consumed.
  EXPECT_EQ(table_.slot_array().used_count(), used_before);
  EXPECT_EQ(table_.Find(4), nullptr);
}

TEST_F(IndirectTableSmallPoolTest, InsertWithIdRollbackOnMempoolExhaustion) {
  // Insert 3 keys to exhaust the mempool.
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);
  ASSERT_NE(table_.Insert(2, 200), SlotArr::kInvalidId);
  ASSERT_NE(table_.Insert(3, 300), SlotArr::kInvalidId);

  uint32_t rc_before = table_.slot_array().RefCount(vid);

  // InsertWithId should fail — mempool exhausted.
  EXPECT_FALSE(table_.InsertWithId(4, vid));

  // Refcount should be rolled back.
  EXPECT_EQ(table_.slot_array().RefCount(vid), rc_before);
  EXPECT_EQ(table_.Find(4), nullptr);
}

// ---------- 7. RCU retire callback -------------------------------------------

TEST_F(IndirectTableTest, RcuRetireReleasesRefcountAfterGracePeriod) {
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);
  EXPECT_EQ(table_.slot_array().RefCount(vid), 1u);
  EXPECT_EQ(table_.slot_array().used_count(), 1u);

  // Remove the key — refcount release is deferred until grace period.
  EXPECT_TRUE(table_.Remove(1));
  EXPECT_EQ(table_.Find(1), nullptr);

  // Refcount should still be 1 immediately after Remove (deferred).
  // We need to flush the grace period to see the release.
  bool done = false;
  // Use a sentinel: poll until used_count drops to 0.
  for (int i = 0; i < 200; ++i) {
    ProgressGracePeriod();
    io_ctx_.restart();
    io_ctx_.poll();
    if (table_.slot_array().used_count() == 0) {
      done = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_TRUE(done) << "Grace period callback did not fire";
  EXPECT_EQ(table_.slot_array().used_count(), 0u);
}

TEST_F(IndirectTableTest, RcuRetireReturnsKeyEntryToMempool) {
  // Insert and remove — after grace period, the KeyEntry should be back
  // in the mempool, allowing a new insert.
  uint32_t vid = table_.Insert(1, 100);
  ASSERT_NE(vid, SlotArr::kInvalidId);
  EXPECT_TRUE(table_.Remove(1));

  // Flush grace period.
  bool done = false;
  for (int i = 0; i < 200; ++i) {
    ProgressGracePeriod();
    io_ctx_.restart();
    io_ctx_.poll();
    if (table_.slot_array().used_count() == 0) {
      done = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ASSERT_TRUE(done);

  // Should be able to insert again (KeyEntry recycled).
  uint32_t vid2 = table_.Insert(1, 100);
  EXPECT_NE(vid2, SlotArr::kInvalidId);
}

// ---------- 8. UpdateValue propagates to all keys sharing the slot -----------

TEST_F(IndirectTableTest, UpdateValuePropagatesToAllKeys) {
  uint32_t vid1 = table_.Insert(1, 42);
  uint32_t vid2 = table_.Insert(2, 42);
  ASSERT_NE(vid1, SlotArr::kInvalidId);
  ASSERT_EQ(vid1, vid2);  // dedup

  // Update the shared value.
  table_.UpdateValue(vid1, 99);

  // Both keys should see the new value.
  auto* e1 = table_.Find(1);
  auto* e2 = table_.Find(2);
  ASSERT_NE(e1, nullptr);
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(*table_.slot_array().Get(e1->value_id), 99u);
  EXPECT_EQ(*table_.slot_array().Get(e2->value_id), 99u);
}

// ---------- 9. ForEachKey visits all inserted keys ---------------------------

TEST_F(IndirectTableTest, ForEachKeyVisitsAllKeys) {
  table_.Insert(10, 100);
  table_.Insert(20, 200);
  table_.Insert(30, 300);

  std::vector<std::pair<uint32_t, uint32_t>> visited;
  table_.ForEachKey([&](const uint32_t& key, uint32_t value_id) {
    visited.emplace_back(key, value_id);
  });

  EXPECT_EQ(visited.size(), 3u);

  // Verify all keys are present (order may vary).
  bool found10 = false, found20 = false, found30 = false;
  for (auto& [k, v] : visited) {
    if (k == 10) found10 = true;
    if (k == 20) found20 = true;
    if (k == 30) found30 = true;
  }
  EXPECT_TRUE(found10);
  EXPECT_TRUE(found20);
  EXPECT_TRUE(found30);
}

// ---------- 10. Prefetch/FindWithPrefetch returns correct results -------------

TEST_F(IndirectTableTest, PrefetchAndFindWithPrefetch) {
  uint32_t vid = table_.Insert(7, 77);
  ASSERT_NE(vid, SlotArr::kInvalidId);

  TestTable::PrefetchContext ctx{};
  table_.Prefetch(7, ctx);
  auto* entry = table_.FindWithPrefetch(7, ctx);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->key, 7u);
  EXPECT_EQ(entry->value_id, vid);
  EXPECT_EQ(*table_.slot_array().Get(vid), 77u);

  // Non-existent key.
  TestTable::PrefetchContext ctx2{};
  table_.Prefetch(999, ctx2);
  EXPECT_EQ(table_.FindWithPrefetch(999, ctx2), nullptr);
}

}  // namespace
}  // namespace indirect_table
