// rxtx/list_slab_test.cc
// Unit tests for the ListSlab slab allocator.

#include "rxtx/list_slab.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/intrusive/slist.hpp>
#include <gtest/gtest.h>

namespace rxtx {
namespace {

// Local test entry type that satisfies ListSlab's static_assert requirements:
// - slist_member_hook<> named 'hook' at offset 0
// - sizeof == 64 (one cache line)
struct alignas(64) TestEntry {
  boost::intrusive::slist_member_hook<> hook;
  uint8_t data[64 - sizeof(boost::intrusive::slist_member_hook<>)];
};
static_assert(sizeof(TestEntry) == 64);

// --- Construction tests ---

TEST(ListSlabTest, ConstructionCapacity1) {
  ListSlab<sizeof(TestEntry)> slab(1);
  EXPECT_EQ(slab.capacity(), std::size_t{1});
  EXPECT_EQ(slab.free_count(), std::size_t{1});
  EXPECT_EQ(slab.used_count(), std::size_t{0});
}

TEST(ListSlabTest, ConstructionCapacity64) {
  ListSlab<sizeof(TestEntry)> slab(64);
  EXPECT_EQ(slab.capacity(), std::size_t{64});
  EXPECT_EQ(slab.free_count(), std::size_t{64});
  EXPECT_EQ(slab.used_count(), std::size_t{0});
}

TEST(ListSlabTest, ConstructionCapacity1024) {
  ListSlab<sizeof(TestEntry)> slab(1024);
  EXPECT_EQ(slab.capacity(), std::size_t{1024});
  EXPECT_EQ(slab.free_count(), std::size_t{1024});
  EXPECT_EQ(slab.used_count(), std::size_t{0});
}

// --- Exhaustion test ---

TEST(ListSlabTest, ExhaustionReturnsNullptr) {
  constexpr std::size_t kCapacity = 16;
  ListSlab<sizeof(TestEntry)> slab(kCapacity);

  std::vector<TestEntry*> entries;
  entries.reserve(kCapacity);

  for (std::size_t i = 0; i < kCapacity; ++i) {
    TestEntry* e = slab.Allocate<TestEntry>();
    ASSERT_NE(e, nullptr) << "Allocate returned nullptr at index " << i;
    entries.push_back(e);
  }

  EXPECT_EQ(slab.free_count(), std::size_t{0});
  EXPECT_EQ(slab.used_count(), kCapacity);

  // Next allocation must fail.
  EXPECT_EQ(slab.Allocate<TestEntry>(), nullptr);
}

// --- Deallocate and reuse test ---

TEST(ListSlabTest, DeallocateAndReuse) {
  ListSlab<sizeof(TestEntry)> slab(4);

  TestEntry* e1 = slab.Allocate<TestEntry>();
  ASSERT_NE(e1, nullptr);
  EXPECT_EQ(slab.free_count(), std::size_t{3});
  EXPECT_EQ(slab.used_count(), std::size_t{1});

  slab.Deallocate<TestEntry>(e1);
  EXPECT_EQ(slab.free_count(), std::size_t{4});
  EXPECT_EQ(slab.used_count(), std::size_t{0});

  // Allocate again — should succeed and return a valid pointer.
  TestEntry* e2 = slab.Allocate<TestEntry>();
  ASSERT_NE(e2, nullptr);
  EXPECT_EQ(slab.free_count(), std::size_t{3});
  EXPECT_EQ(slab.used_count(), std::size_t{1});

  // Write to the entry to verify the memory is usable.
  e2->data[0] = 0xAB;
  EXPECT_EQ(e2->data[0], 0xAB);
}

// --- Capacity 0 test ---

TEST(ListSlabTest, CapacityZeroAlwaysReturnsNullptr) {
  ListSlab<sizeof(TestEntry)> slab(0);
  EXPECT_EQ(slab.capacity(), std::size_t{0});
  EXPECT_EQ(slab.free_count(), std::size_t{0});
  EXPECT_EQ(slab.used_count(), std::size_t{0});

  EXPECT_EQ(slab.Allocate<TestEntry>(), nullptr);
  EXPECT_EQ(slab.Allocate<TestEntry>(), nullptr);
}

}  // namespace
}  // namespace rxtx

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
