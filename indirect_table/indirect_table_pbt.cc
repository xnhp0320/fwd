#include "indirect_table/indirect_table.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <rte_rcu_qsbr.h>

#include "boost/asio/io_context.hpp"
#include "rcu/rcu_manager.h"
#include "rxtx/test_utils.h"

namespace indirect_table {
namespace {

using TestTable = IndirectTable<uint32_t, uint32_t>;
using SlotArr = TestTable::SlotArrayType;

// Operation types for the random sequence.
enum class OpType : uint8_t { Insert, InsertWithId, Remove };

struct Op {
  OpType type;
  uint32_t key;
  uint32_t value;
};

// ---------------------------------------------------------------------------
// Feature: indirect-table, Property P1: Refcount Invariant
// After any random sequence of Insert/InsertWithId/Remove operations on
// IndirectTable, verify that each slot's refcount equals the number of
// KeyEntries referencing it.
// **Validates: Requirements 3.1, 3.2, 3.3, 14.1, 14.2, 14.3**
// ---------------------------------------------------------------------------

}  // namespace
}  // namespace indirect_table

namespace rc {

template <>
struct Arbitrary<indirect_table::Op> {
  static Gen<indirect_table::Op> arbitrary() {
    return gen::construct<indirect_table::Op>(
        gen::element(indirect_table::OpType::Insert,
                     indirect_table::OpType::InsertWithId,
                     indirect_table::OpType::Remove),
        gen::inRange<uint32_t>(0, 32),
        gen::inRange<uint32_t>(0, 8));
  }
};

}  // namespace rc

namespace indirect_table {
namespace {

class IndirectTablePbtFixture : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }
};

// Use a regular TEST_F with rc::check so we control resource lifetime.
// A single RcuManager + IndirectTable is created once, and each RC iteration
// inserts/removes keys then verifies the invariant. Between iterations we
// remove all remaining keys and flush the grace period to reset state.
TEST_F(IndirectTablePbtFixture, RefcountInvariant) {
  boost::asio::io_context io_ctx;
  rcu::RcuManager rcu_manager;

  rcu::RcuManager::Config rcu_cfg;
  rcu_cfg.max_threads = 8;
  rcu_cfg.poll_interval_ms = 1;
  ASSERT_TRUE(rcu_manager.Init(io_ctx, rcu_cfg).ok());
  ASSERT_TRUE(rcu_manager.RegisterThread(0).ok());
  ASSERT_TRUE(rcu_manager.Start().ok());

  TestTable table;
  TestTable::Config cfg;
  cfg.value_capacity = 8;
  cfg.value_bucket_count = 8;
  cfg.key_capacity = 31;  // rte_mempool requires (2^n - 1)
  cfg.key_bucket_count = 8;
  cfg.name = "pbt_rc";
  ASSERT_TRUE(table.Init(cfg, &rcu_manager).ok());

  // Helper to flush all pending RCU grace period callbacks.
  auto flush_grace_period = [&]() {
    for (int i = 0; i < 20; ++i) {
      rte_rcu_qsbr_quiescent(rcu_manager.GetQsbrVar(), 0);
      io_ctx.restart();
      io_ctx.run_for(std::chrono::milliseconds(5));
    }
  };

  rc::check([&](const std::vector<Op>& ops) {
    RC_PRE(!ops.empty());

    // Shadow model: key -> value_id
    std::unordered_map<uint32_t, uint32_t> shadow;

    // Execute operations.
    for (const auto& op : ops) {
      switch (op.type) {
        case OpType::Insert: {
          uint32_t vid = table.Insert(op.key, op.value);
          if (vid != SlotArr::kInvalidId) {
            shadow[op.key] = vid;
          }
          break;
        }
        case OpType::InsertWithId: {
          if (shadow.empty()) break;
          auto it = shadow.begin();
          uint32_t steps = op.value % shadow.size();
          std::advance(it, steps);
          uint32_t existing_vid = it->second;

          bool ok = table.InsertWithId(op.key, existing_vid);
          if (ok) {
            shadow[op.key] = existing_vid;
          }
          break;
        }
        case OpType::Remove: {
          bool removed = table.Remove(op.key);
          if (removed) {
            shadow.erase(op.key);
          }
          break;
        }
      }
    }

    // Flush RCU grace period callbacks.
    flush_grace_period();

    // Verify refcount invariant.
    std::unordered_map<uint32_t, uint32_t> expected_refcount;
    for (const auto& [key, vid] : shadow) {
      expected_refcount[vid]++;
    }

    for (uint32_t id = 0; id < cfg.value_capacity; ++id) {
      uint32_t actual_rc = table.slot_array().RefCount(id);
      auto it = expected_refcount.find(id);
      uint32_t expected_rc = (it != expected_refcount.end()) ? it->second : 0;
      RC_ASSERT(actual_rc == expected_rc);
    }

    // Clean up: remove all remaining keys to reset state for next iteration.
    for (const auto& [key, vid] : shadow) {
      table.Remove(key);
    }
    flush_grace_period();

    // Verify everything is fully cleaned up.
    RC_ASSERT(table.slot_array().used_count() == 0u);
  });

  rcu_manager.Stop();
  (void)rcu_manager.UnregisterThread(0);
}

// ---------------------------------------------------------------------------
// Feature: indirect-table, Property P2: Value Deduplication
// After any random sequence of FindOrAllocate calls on a SlotArray, no two
// in-use slots contain the same value.
// **Validates: Requirements 5.1, 5.2**
// ---------------------------------------------------------------------------

TEST_F(IndirectTablePbtFixture, ValueDeduplication) {
  SlotArr slot_array;
  SlotArr::Config sa_cfg;
  sa_cfg.capacity = 8;
  sa_cfg.bucket_count = 8;
  sa_cfg.name = "pbt_dedup";
  ASSERT_TRUE(slot_array.Init(sa_cfg).ok());

  rc::check([&](const std::vector<uint32_t>& raw_values) {
    RC_PRE(!raw_values.empty());

    // Constrain values to [0, 8) range.
    std::vector<uint32_t> values;
    values.reserve(raw_values.size());
    for (uint32_t v : raw_values) {
      values.push_back(v % 8);
    }

    // Track allocated slot IDs for cleanup.
    std::vector<uint32_t> allocated_ids;

    // Execute FindOrAllocate for each value.
    for (uint32_t val : values) {
      uint32_t id = slot_array.FindOrAllocate(val);
      if (id != SlotArr::kInvalidId) {
        allocated_ids.push_back(id);
      }
    }

    // Verify: no two in-use slots contain the same value.
    std::unordered_map<uint32_t, uint32_t> value_to_slot;  // value -> slot id
    slot_array.ForEachInUse([&](uint32_t id, const uint32_t& value) {
      auto [it, inserted] = value_to_slot.emplace(value, id);
      RC_ASSERT(inserted);  // If not inserted, a duplicate value exists.
    });

    // Clean up: release all refs and deallocate slots with refcount 0.
    // Each FindOrAllocate added a ref, so release once per call.
    for (uint32_t id : allocated_ids) {
      if (slot_array.Release(id)) {
        slot_array.Deallocate(id);
      }
    }

    // Verify full cleanup.
    RC_ASSERT(slot_array.used_count() == 0u);
  });
}

}  // namespace
}  // namespace indirect_table
