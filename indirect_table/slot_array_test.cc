#include "indirect_table/slot_array.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "rxtx/test_utils.h"

namespace indirect_table {
namespace {

using TestSlotArray = SlotArray<uint32_t>;

class SlotArrayTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }

  void SetUp() override {
    TestSlotArray::Config cfg;
    cfg.capacity = 16;
    cfg.bucket_count = 16;
    cfg.name = "test_slot_array";
    ASSERT_TRUE(sa_.Init(cfg).ok());
  }

  TestSlotArray sa_;
};

// ---------- 1. Init sets used_count=0 and capacity correctly ----------------

TEST_F(SlotArrayTest, InitSetsUsedCountAndCapacity) {
  EXPECT_EQ(sa_.capacity(), 16u);
  EXPECT_EQ(sa_.used_count(), 0u);
}

// ---------- 2. Allocate/Deallocate cycle, free stack exhaustion -------------

TEST_F(SlotArrayTest, AllocateDeallocateCycle) {
  uint32_t id = sa_.Allocate();
  ASSERT_NE(id, TestSlotArray::kInvalidId);
  EXPECT_EQ(sa_.used_count(), 1u);
  EXPECT_EQ(sa_.RefCount(id), 1u);

  // Release to refcount 0, then deallocate.
  EXPECT_TRUE(sa_.Release(id));
  sa_.Deallocate(id);
  EXPECT_EQ(sa_.used_count(), 0u);
}

TEST_F(SlotArrayTest, FreeStackExhaustionReturnsInvalidId) {
  std::vector<uint32_t> ids;
  for (uint32_t i = 0; i < 16; ++i) {
    uint32_t id = sa_.Allocate();
    ASSERT_NE(id, TestSlotArray::kInvalidId);
    ids.push_back(id);
  }
  EXPECT_EQ(sa_.used_count(), 16u);

  // Next allocation should fail.
  EXPECT_EQ(sa_.Allocate(), TestSlotArray::kInvalidId);
  EXPECT_EQ(sa_.FindOrAllocate(999), TestSlotArray::kInvalidId);

  // Free one and verify we can allocate again.
  EXPECT_TRUE(sa_.Release(ids[0]));
  sa_.Deallocate(ids[0]);
  EXPECT_NE(sa_.Allocate(), TestSlotArray::kInvalidId);
}

// ---------- 3. FindOrAllocate dedup ----------------------------------------

TEST_F(SlotArrayTest, FindOrAllocateDedupSameValueReturnsSameId) {
  uint32_t id1 = sa_.FindOrAllocate(42);
  ASSERT_NE(id1, TestSlotArray::kInvalidId);
  EXPECT_EQ(sa_.RefCount(id1), 1u);

  // Same value should return same ID with incremented refcount.
  uint32_t id2 = sa_.FindOrAllocate(42);
  EXPECT_EQ(id1, id2);
  EXPECT_EQ(sa_.RefCount(id1), 2u);
  EXPECT_EQ(sa_.used_count(), 1u);

  // Different value should return different ID.
  uint32_t id3 = sa_.FindOrAllocate(99);
  ASSERT_NE(id3, TestSlotArray::kInvalidId);
  EXPECT_NE(id3, id1);
  EXPECT_EQ(sa_.RefCount(id3), 1u);
  EXPECT_EQ(sa_.used_count(), 2u);
}

// ---------- 4. AddRef/Release lifecycle ------------------------------------

TEST_F(SlotArrayTest, AddRefReleasLifecycle) {
  uint32_t id = sa_.FindOrAllocate(10);
  ASSERT_NE(id, TestSlotArray::kInvalidId);
  EXPECT_EQ(sa_.RefCount(id), 1u);

  sa_.AddRef(id);
  EXPECT_EQ(sa_.RefCount(id), 2u);

  sa_.AddRef(id);
  EXPECT_EQ(sa_.RefCount(id), 3u);

  // Release returns false while refcount > 0 after decrement.
  EXPECT_FALSE(sa_.Release(id));
  EXPECT_EQ(sa_.RefCount(id), 2u);

  EXPECT_FALSE(sa_.Release(id));
  EXPECT_EQ(sa_.RefCount(id), 1u);

  // Release returns true when refcount hits 0.
  EXPECT_TRUE(sa_.Release(id));
  EXPECT_EQ(sa_.RefCount(id), 0u);
}

// ---------- 5. FindByValue -------------------------------------------------

TEST_F(SlotArrayTest, FindByValueReturnsCorrectIdOrInvalid) {
  // Not present yet.
  EXPECT_EQ(sa_.FindByValue(77), TestSlotArray::kInvalidId);

  uint32_t id = sa_.FindOrAllocate(77);
  ASSERT_NE(id, TestSlotArray::kInvalidId);

  EXPECT_EQ(sa_.FindByValue(77), id);
  EXPECT_EQ(sa_.FindByValue(78), TestSlotArray::kInvalidId);

  // After release + deallocate, value should no longer be found.
  EXPECT_TRUE(sa_.Release(id));
  sa_.Deallocate(id);
  EXPECT_EQ(sa_.FindByValue(77), TestSlotArray::kInvalidId);
}

// ---------- 6. UpdateValue rehashes correctly in reverse map ---------------

TEST_F(SlotArrayTest, UpdateValueRehashesCorrectly) {
  uint32_t id = sa_.FindOrAllocate(100);
  ASSERT_NE(id, TestSlotArray::kInvalidId);

  EXPECT_EQ(sa_.FindByValue(100), id);

  sa_.UpdateValue(id, 200);

  // Old value should not be found; new value should map to same ID.
  EXPECT_EQ(sa_.FindByValue(100), TestSlotArray::kInvalidId);
  EXPECT_EQ(sa_.FindByValue(200), id);
  EXPECT_EQ(*sa_.Get(id), 200u);
}

// ---------- 7. ForEachInUse visits only in-use slots -----------------------

TEST_F(SlotArrayTest, ForEachInUseVisitsOnlyInUseSlots) {
  uint32_t id1 = sa_.FindOrAllocate(10);
  uint32_t id2 = sa_.FindOrAllocate(20);
  uint32_t id3 = sa_.FindOrAllocate(30);
  ASSERT_NE(id1, TestSlotArray::kInvalidId);
  ASSERT_NE(id2, TestSlotArray::kInvalidId);
  ASSERT_NE(id3, TestSlotArray::kInvalidId);

  // Release and deallocate id2.
  EXPECT_TRUE(sa_.Release(id2));
  sa_.Deallocate(id2);

  std::vector<std::pair<uint32_t, uint32_t>> visited;
  sa_.ForEachInUse([&](uint32_t id, const uint32_t& value) {
    visited.emplace_back(id, value);
  });

  EXPECT_EQ(visited.size(), 2u);

  // Verify the visited entries match the in-use slots (order may vary).
  bool found_id1 = false, found_id3 = false;
  for (auto& [vid, vval] : visited) {
    if (vid == id1) {
      EXPECT_EQ(vval, 10u);
      found_id1 = true;
    } else if (vid == id3) {
      EXPECT_EQ(vval, 30u);
      found_id3 = true;
    }
  }
  EXPECT_TRUE(found_id1);
  EXPECT_TRUE(found_id3);
}

// ---------- 8. Get returns valid pointer for allocated slots ---------------

TEST_F(SlotArrayTest, GetReturnsValidPointer) {
  uint32_t id = sa_.FindOrAllocate(55);
  ASSERT_NE(id, TestSlotArray::kInvalidId);

  uint32_t* ptr = sa_.Get(id);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 55u);

  // Const overload.
  const TestSlotArray& csa = sa_;
  const uint32_t* cptr = csa.Get(id);
  ASSERT_NE(cptr, nullptr);
  EXPECT_EQ(*cptr, 55u);
}

}  // namespace
}  // namespace indirect_table
