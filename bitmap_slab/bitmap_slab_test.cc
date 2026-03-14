// bitmap_slab/bitmap_slab_test.cc

#include "bitmap_slab/bitmap_slab.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include <gtest/gtest.h>

namespace bitmap_slab {
namespace {

// 24-byte test object.
struct Obj24 {
  uint8_t data[24];
};
static_assert(sizeof(Obj24) == 24);

// 48-byte test object.
struct Obj48 {
  uint8_t data[48];
};
static_assert(sizeof(Obj48) == 48);

// --- Basic construction ---

TEST(BitmapSlabTest, ConstructionCapacity1) {
  BitmapSlab<sizeof(Obj24)> slab(1);
  EXPECT_EQ(slab.capacity(), 1u);
  EXPECT_EQ(slab.free_count(), 1u);
  EXPECT_EQ(slab.used_count(), 0u);
}

TEST(BitmapSlabTest, ConstructionCapacity64) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  EXPECT_EQ(slab.capacity(), 64u);
  EXPECT_EQ(slab.free_count(), 64u);
  EXPECT_EQ(slab.used_count(), 0u);
}

TEST(BitmapSlabTest, ConstructionCapacity100) {
  BitmapSlab<sizeof(Obj24)> slab(100);
  EXPECT_EQ(slab.capacity(), 100u);
  EXPECT_EQ(slab.free_count(), 100u);
}

// --- Single allocation ---

TEST(BitmapSlabTest, AllocateSingle) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  Obj24* p = slab.Allocate<Obj24>();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(slab.free_count(), 63u);
  EXPECT_EQ(slab.used_count(), 1u);

  // Write to verify memory is usable.
  p->data[0] = 0xAB;
  EXPECT_EQ(p->data[0], 0xAB);

  slab.Deallocate(p);
  EXPECT_EQ(slab.free_count(), 64u);
}

// --- Contiguous N allocation ---

TEST(BitmapSlabTest, AllocateN_Basic) {
  BitmapSlab<sizeof(Obj24)> slab(256);
  auto span = slab.AllocateN<Obj24>(8);
  ASSERT_TRUE(span);
  EXPECT_EQ(span.count, 8u);
  EXPECT_EQ(slab.free_count(), 248u);

  // Verify contiguity: each element is exactly 24 bytes apart.
  for (std::size_t i = 1; i < span.count; ++i) {
    auto diff = reinterpret_cast<uintptr_t>(&span[i]) -
                reinterpret_cast<uintptr_t>(&span[i - 1]);
    EXPECT_EQ(diff, sizeof(Obj24));
  }

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 256u);
}

TEST(BitmapSlabTest, AllocateN_48Byte) {
  BitmapSlab<sizeof(Obj48)> slab(128);
  auto span = slab.AllocateN<Obj48>(16);
  ASSERT_TRUE(span);
  EXPECT_EQ(span.count, 16u);

  for (std::size_t i = 0; i < span.count; ++i) {
    span[i].data[0] = static_cast<uint8_t>(i);
  }
  for (std::size_t i = 0; i < span.count; ++i) {
    EXPECT_EQ(span[i].data[0], static_cast<uint8_t>(i));
  }

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 128u);
}

// --- Exhaustion ---

TEST(BitmapSlabTest, ExhaustionReturnsNull) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  auto span = slab.AllocateN<Obj24>(64);
  ASSERT_TRUE(span);
  EXPECT_EQ(slab.free_count(), 0u);

  // Next allocation must fail.
  EXPECT_EQ(slab.Allocate<Obj24>(), nullptr);
  auto span2 = slab.AllocateN<Obj24>(1);
  EXPECT_FALSE(span2);

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 64u);
}

// --- Fragmentation: allocate-free-allocate pattern ---

TEST(BitmapSlabTest, FragmentationAndCoalescing) {
  BitmapSlab<sizeof(Obj24)> slab(128);

  // Allocate 4 blocks of 32.
  std::vector<Span<Obj24>> blocks;
  for (int i = 0; i < 4; ++i) {
    auto s = slab.AllocateN<Obj24>(32);
    ASSERT_TRUE(s);
    blocks.push_back(s);
  }
  EXPECT_EQ(slab.free_count(), 0u);

  // Free blocks 0 and 2 (creating two non-adjacent holes of 32).
  slab.DeallocateN(blocks[0].data, blocks[0].count);
  slab.DeallocateN(blocks[2].data, blocks[2].count);
  EXPECT_EQ(slab.free_count(), 64u);

  // Allocating 33 contiguous should fail (max hole is 32).
  auto big = slab.AllocateN<Obj24>(33);
  EXPECT_FALSE(big);

  // Allocating 32 should succeed (fits in one hole).
  auto fit = slab.AllocateN<Obj24>(32);
  ASSERT_TRUE(fit);
  EXPECT_EQ(slab.free_count(), 32u);

  // Clean up.
  slab.DeallocateN(fit.data, fit.count);
  slab.DeallocateN(blocks[1].data, blocks[1].count);
  slab.DeallocateN(blocks[3].data, blocks[3].count);
  EXPECT_EQ(slab.free_count(), 128u);
}

// --- Free middle, then allocate across boundary ---

TEST(BitmapSlabTest, FreeMiddleAndReallocate) {
  BitmapSlab<sizeof(Obj24)> slab(128);

  // Allocate 3 blocks: [0..31], [32..63], [64..95].
  auto a = slab.AllocateN<Obj24>(32);
  auto b = slab.AllocateN<Obj24>(32);
  auto c = slab.AllocateN<Obj24>(32);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);

  // Free the middle block.
  slab.DeallocateN(b.data, b.count);
  EXPECT_EQ(slab.free_count(), 64u);  // 32 freed + 32 never allocated

  // Allocate 32 — should reuse the freed middle.
  auto d = slab.AllocateN<Obj24>(32);
  ASSERT_TRUE(d);

  // Clean up.
  slab.DeallocateN(a.data, a.count);
  slab.DeallocateN(c.data, c.count);
  slab.DeallocateN(d.data, d.count);
  EXPECT_EQ(slab.free_count(), 128u);
}

// --- AllocateN(0) returns null span ---

TEST(BitmapSlabTest, AllocateZeroReturnsNull) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  auto span = slab.AllocateN<Obj24>(0);
  EXPECT_FALSE(span);
  EXPECT_EQ(slab.free_count(), 64u);
}

// --- Allocate more than capacity ---

TEST(BitmapSlabTest, AllocateMoreThanCapacity) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  auto span = slab.AllocateN<Obj24>(65);
  EXPECT_FALSE(span);
  EXPECT_EQ(slab.free_count(), 64u);
}

// --- Multiple small allocations, all unique addresses ---

TEST(BitmapSlabTest, AllAddressesUnique) {
  constexpr std::size_t kCap = 256;
  BitmapSlab<sizeof(Obj24)> slab(kCap);

  std::set<Obj24*> addrs;
  for (std::size_t i = 0; i < kCap; ++i) {
    Obj24* p = slab.Allocate<Obj24>();
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(addrs.insert(p).second) << "Duplicate address at i=" << i;
  }
  EXPECT_EQ(slab.free_count(), 0u);

  for (Obj24* p : addrs) {
    slab.Deallocate(p);
  }
  EXPECT_EQ(slab.free_count(), kCap);
}

// --- Cross word-boundary allocation ---

TEST(BitmapSlabTest, CrossWordBoundary) {
  // 128 slots = 2 bitmap words. Allocate 60, then allocate 8 which
  // must span the word boundary (bits 60..67).
  BitmapSlab<sizeof(Obj24)> slab(128);

  auto first = slab.AllocateN<Obj24>(60);
  ASSERT_TRUE(first);

  auto cross = slab.AllocateN<Obj24>(8);
  ASSERT_TRUE(cross);
  EXPECT_EQ(cross.count, 8u);

  // Verify contiguity.
  for (std::size_t i = 1; i < cross.count; ++i) {
    auto diff = reinterpret_cast<uintptr_t>(&cross[i]) -
                reinterpret_cast<uintptr_t>(&cross[i - 1]);
    EXPECT_EQ(diff, sizeof(Obj24));
  }

  slab.DeallocateN(first.data, first.count);
  slab.DeallocateN(cross.data, cross.count);
}

// --- Span iteration ---

TEST(BitmapSlabTest, SpanIteration) {
  BitmapSlab<sizeof(Obj24)> slab(64);
  auto span = slab.AllocateN<Obj24>(10);
  ASSERT_TRUE(span);

  std::size_t count = 0;
  for (auto& obj : span) {
    obj.data[0] = static_cast<uint8_t>(count++);
  }
  EXPECT_EQ(count, 10u);

  for (std::size_t i = 0; i < span.count; ++i) {
    EXPECT_EQ(span[i].data[0], static_cast<uint8_t>(i));
  }

  slab.DeallocateN(span.data, span.count);
}

// --- Repeated alloc/dealloc cycles ---

TEST(BitmapSlabTest, RepeatedCycles) {
  BitmapSlab<sizeof(Obj48)> slab(256);

  for (int cycle = 0; cycle < 100; ++cycle) {
    auto span = slab.AllocateN<Obj48>(16);
    ASSERT_TRUE(span) << "Failed at cycle " << cycle;
    slab.DeallocateN(span.data, span.count);
  }
  EXPECT_EQ(slab.free_count(), 256u);
}

// --- N > 64 tests ---

TEST(BitmapSlabTest, AllocateN_GreaterThan64) {
  BitmapSlab<sizeof(Obj24)> slab(512);
  auto span = slab.AllocateN<Obj24>(128);
  ASSERT_TRUE(span);
  EXPECT_EQ(span.count, 128u);
  EXPECT_EQ(slab.free_count(), 384u);

  // Verify contiguity.
  for (std::size_t i = 1; i < span.count; ++i) {
    auto diff = reinterpret_cast<uintptr_t>(&span[i]) -
                reinterpret_cast<uintptr_t>(&span[i - 1]);
    EXPECT_EQ(diff, sizeof(Obj24));
  }

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 512u);
}

TEST(BitmapSlabTest, AllocateN_200) {
  BitmapSlab<sizeof(Obj24)> slab(1024);
  auto span = slab.AllocateN<Obj24>(200);
  ASSERT_TRUE(span);
  EXPECT_EQ(span.count, 200u);
  EXPECT_EQ(slab.free_count(), 824u);

  for (std::size_t i = 1; i < span.count; ++i) {
    auto diff = reinterpret_cast<uintptr_t>(&span[i]) -
                reinterpret_cast<uintptr_t>(&span[i - 1]);
    EXPECT_EQ(diff, sizeof(Obj24));
  }

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 1024u);
}

TEST(BitmapSlabTest, AllocateN_ExactlyCapacity256) {
  BitmapSlab<sizeof(Obj48)> slab(256);
  auto span = slab.AllocateN<Obj48>(256);
  ASSERT_TRUE(span);
  EXPECT_EQ(span.count, 256u);
  EXPECT_EQ(slab.free_count(), 0u);

  EXPECT_FALSE(slab.AllocateN<Obj48>(1));

  slab.DeallocateN(span.data, span.count);
  EXPECT_EQ(slab.free_count(), 256u);
}

// Fragmentation with N > 64: create a hole larger than 64 and allocate into it.
TEST(BitmapSlabTest, FragmentationN_GreaterThan64) {
  BitmapSlab<sizeof(Obj24)> slab(512);

  // Allocate 4 blocks of 128.
  auto a = slab.AllocateN<Obj24>(128);
  auto b = slab.AllocateN<Obj24>(128);
  auto c = slab.AllocateN<Obj24>(128);
  auto d = slab.AllocateN<Obj24>(128);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  ASSERT_TRUE(d);
  EXPECT_EQ(slab.free_count(), 0u);

  // Free b and c — creates a 256-slot hole in the middle.
  slab.DeallocateN(b.data, b.count);
  slab.DeallocateN(c.data, c.count);
  EXPECT_EQ(slab.free_count(), 256u);

  // Allocating 200 contiguous should succeed (fits in the 256 hole).
  auto big = slab.AllocateN<Obj24>(200);
  ASSERT_TRUE(big);
  EXPECT_EQ(big.count, 200u);

  // Allocating another 100 should fail (only 56 left in the hole).
  auto fail = slab.AllocateN<Obj24>(100);
  EXPECT_FALSE(fail);

  // Clean up.
  slab.DeallocateN(big.data, big.count);
  slab.DeallocateN(a.data, a.count);
  slab.DeallocateN(d.data, d.count);
  EXPECT_EQ(slab.free_count(), 512u);
}

}  // namespace
}  // namespace bitmap_slab

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
