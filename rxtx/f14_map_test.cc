#include "rxtx/f14_map.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

namespace rxtx::f14 {
namespace {

// ---------------------------------------------------------------------------
// Static layout assertions (unit tests for Task 2.1 data structures)
// ---------------------------------------------------------------------------

TEST(ChunkHeaderTest, SizeAndAlignment) {
  static_assert(sizeof(ChunkHeader) == 16);
  static_assert(alignof(ChunkHeader) == 16);
  // control at offset 14, overflow at offset 15
  static_assert(offsetof(ChunkHeader, control) == 14);
  static_assert(offsetof(ChunkHeader, overflow) == 15);
}

TEST(ChunkTest, SizeForPointerItem) {
  // Chunk<void*> = 16-byte header + 14 × 8-byte pointers = 128 bytes
  static_assert(sizeof(Chunk<void*>) == 128);
  static_assert(alignof(Chunk<void*>) == 128);
}

// ---------------------------------------------------------------------------
// ChunkHeader field operations
// ---------------------------------------------------------------------------

TEST(ChunkTest, TagAccessors) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  EXPECT_FALSE(chunk.SlotUsed(0));
  EXPECT_EQ(chunk.GetTag(0), 0);

  chunk.SetTag(0, 0xAB);
  EXPECT_TRUE(chunk.SlotUsed(0));
  EXPECT_EQ(chunk.GetTag(0), 0xAB);

  chunk.ClearTag(0);
  EXPECT_FALSE(chunk.SlotUsed(0));
  EXPECT_EQ(chunk.GetTag(0), 0);
}

TEST(ChunkTest, OverflowSaturation) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  // Increment to 255
  for (int i = 0; i < 255; ++i) {
    chunk.IncOverflow();
  }
  EXPECT_EQ(chunk.OverflowCount(), 255);

  // Saturates: increment from 255 stays at 255
  chunk.IncOverflow();
  EXPECT_EQ(chunk.OverflowCount(), 255);

  // Saturates: decrement from 255 stays at 255
  chunk.DecOverflow();
  EXPECT_EQ(chunk.OverflowCount(), 255);
}

TEST(ChunkTest, HostedOverflowCount) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  EXPECT_EQ(chunk.HostedOverflowCount(), 0);

  // Increment hosted overflow (delta = +1 in high nibble = +0x10)
  chunk.AdjHostedOverflow(1);
  EXPECT_EQ(chunk.HostedOverflowCount(), 1);

  chunk.AdjHostedOverflow(1);
  EXPECT_EQ(chunk.HostedOverflowCount(), 2);

  // Decrement
  chunk.AdjHostedOverflow(-1);
  EXPECT_EQ(chunk.HostedOverflowCount(), 1);
}

TEST(ChunkTest, ScaleAccessors) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  EXPECT_EQ(chunk.Scale(), 0);

  chunk.SetScale(12);
  EXPECT_EQ(chunk.Scale(), 12);

  // Scale only uses low nibble — should not affect hosted overflow
  chunk.AdjHostedOverflow(3);
  EXPECT_EQ(chunk.Scale(), 12);
  EXPECT_EQ(chunk.HostedOverflowCount(), 3);
}

TEST(ChunkTest, ClearResetsEverything) {
  alignas(128) Chunk<void*> chunk;
  chunk.SetTag(5, 0xFF);
  chunk.SetScale(12);
  chunk.IncOverflow();
  chunk.AdjHostedOverflow(2);

  chunk.Clear();

  EXPECT_EQ(chunk.GetTag(5), 0);
  EXPECT_EQ(chunk.Scale(), 0);
  EXPECT_EQ(chunk.OverflowCount(), 0);
  EXPECT_EQ(chunk.HostedOverflowCount(), 0);
}

TEST(ChunkTest, FirstEmpty) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  // All empty — first empty is slot 0
  EXPECT_EQ(chunk.FirstEmpty(), 0);

  // Fill slots 0–12, first empty should be 13
  for (int i = 0; i < 13; ++i) {
    chunk.SetTag(i, 0x80 + i);
  }
  EXPECT_EQ(chunk.FirstEmpty(), 13);

  // Fill all 14 slots — no empty
  chunk.SetTag(13, 0x80);
  EXPECT_EQ(chunk.FirstEmpty(), -1);
}

TEST(ChunkTest, OccupiedMaskAndTagMatch) {
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  // Set tags at positions 0, 3, 7 with value 0xAB
  chunk.SetTag(0, 0xAB);
  chunk.SetTag(3, 0xAB);
  chunk.SetTag(7, 0xCD);

  TagMask occ = chunk.OccupiedMask();
  EXPECT_EQ(occ, (1u << 0) | (1u << 3) | (1u << 7));

  TagMask match_ab = chunk.TagMatch(0xAB);
  EXPECT_EQ(match_ab, (1u << 0) | (1u << 3));

  TagMask match_cd = chunk.TagMatch(0xCD);
  EXPECT_EQ(match_cd, (1u << 7));

  TagMask match_none = chunk.TagMatch(0xFF);
  EXPECT_EQ(match_none, 0u);
}

// ---------------------------------------------------------------------------
// HashPair / SplitHash / ProbeDelta
// ---------------------------------------------------------------------------

TEST(SplitHashTest, TagAlwaysNonZero) {
  // Tag must have bit 7 set (>= 0x80)
  auto hp = SplitHash(0);
  EXPECT_GE(hp.tag, 0x80);

  hp = SplitHash(0xFFFFFFFF);
  EXPECT_GE(hp.tag, 0x80);

  hp = SplitHash(42);
  EXPECT_GE(hp.tag, 0x80);
}

TEST(SplitHashTest, HashPreserved) {
  auto hp = SplitHash(12345);
  EXPECT_EQ(hp.hash, 12345u);
}

TEST(SplitHashTest, TagDerivation) {
  // tag = (hash >> 24) | 0x80
  std::size_t hash = 0xAB000000ULL;
  auto hp = SplitHash(hash);
  EXPECT_EQ(hp.tag, static_cast<uint8_t>(0xAB | 0x80));
}

TEST(ProbeDeltaTest, AlwaysOdd) {
  for (int tag = 0; tag < 256; ++tag) {
    std::size_t delta = ProbeDelta(static_cast<uint8_t>(tag));
    EXPECT_EQ(delta % 2, 1u) << "delta must be odd for tag=" << tag;
  }
}

TEST(ProbeDeltaTest, Formula) {
  EXPECT_EQ(ProbeDelta(0x80), 2 * 0x80u + 1);
  EXPECT_EQ(ProbeDelta(0xFF), 2 * 0xFFu + 1);
  EXPECT_EQ(ProbeDelta(0), 1u);
}

// ---------------------------------------------------------------------------
// PackedPtr / PackedFromItemPtr
// ---------------------------------------------------------------------------

TEST(PackedPtrTest, DefaultIsZero) {
  PackedPtr p;
  EXPECT_EQ(p.raw, 0u);
}

TEST(PackedPtrTest, Equality) {
  PackedPtr a{42};
  PackedPtr b{42};
  PackedPtr c{99};
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(PackedPtrTest, Ordering) {
  PackedPtr a{10};
  PackedPtr b{20};
  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
}

TEST(PackedPtrTest, EncodingMatchesCFmap) {
  // Verify encoding matches: encoded = index >> 1, raw = ptr | encoded
  // Use a chunk to get a properly aligned item pointer
  alignas(128) Chunk<void*> chunk;
  chunk.Clear();

  for (std::size_t idx = 0; idx < 14; ++idx) {
    void* item_ptr = &chunk.items[idx];
    PackedPtr pp = PackedFromItemPtr(item_ptr, idx);

    std::uintptr_t expected_encoded = idx >> 1;
    std::uintptr_t expected_raw =
        reinterpret_cast<std::uintptr_t>(item_ptr) | expected_encoded;
    EXPECT_EQ(pp.raw, expected_raw) << "index=" << idx;
  }
}

// ---------------------------------------------------------------------------
// DefaultChunkAllocator
// ---------------------------------------------------------------------------

TEST(DefaultChunkAllocatorTest, AllocateAndDeallocate) {
  DefaultChunkAllocator alloc;
  void* ptr = alloc.allocate(128, 128);
  ASSERT_NE(ptr, nullptr);
  // Verify alignment
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % 128, 0u);
  alloc.deallocate(ptr);
}

// ---------------------------------------------------------------------------
// F14Map basic operations — set mode (Key == Value, Item = Key)
// Uses EnableItemIteration=false since sizeof(int) < 8.
// ---------------------------------------------------------------------------

using IntSet = F14Map<int, int, std::hash<int>, std::equal_to<int>,
                      DefaultChunkAllocator, false>;

TEST(F14MapSetTest, DefaultConstructEmpty) {
  IntSet map;
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.Find(42), nullptr);
}

TEST(F14MapSetTest, InsertAndFind) {
  IntSet map;
  auto [val, inserted] = map.Insert(1, 1);
  ASSERT_NE(val, nullptr);
  EXPECT_TRUE(inserted);
  EXPECT_EQ(*val, 1);
  EXPECT_EQ(map.size(), 1u);

  int* found = map.Find(1);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, 1);
}

TEST(F14MapSetTest, InsertDuplicate) {
  IntSet map;
  auto [val1, ins1] = map.Insert(1, 1);
  EXPECT_TRUE(ins1);

  auto [val2, ins2] = map.Insert(1, 1);
  EXPECT_FALSE(ins2);
  EXPECT_EQ(val1, val2);
  EXPECT_EQ(*val2, 1);
  EXPECT_EQ(map.size(), 1u);
}

TEST(F14MapSetTest, FindMissing) {
  IntSet map;
  map.Insert(1, 1);
  EXPECT_EQ(map.Find(2), nullptr);
  EXPECT_EQ(map.Find(999), nullptr);
}

TEST(F14MapSetTest, ConstFind) {
  IntSet map;
  map.Insert(1, 1);
  const auto& cmap = map;
  const int* found = cmap.Find(1);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, 1);
  EXPECT_EQ(cmap.Find(2), nullptr);
}

TEST(F14MapSetTest, EraseExisting) {
  IntSet map;
  map.Insert(1, 1);
  map.Insert(2, 2);
  EXPECT_EQ(map.size(), 2u);

  EXPECT_TRUE(map.Erase(1));
  EXPECT_EQ(map.size(), 1u);
  EXPECT_EQ(map.Find(1), nullptr);

  int* found = map.Find(2);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, 2);
}

TEST(F14MapSetTest, EraseNonExisting) {
  IntSet map;
  map.Insert(1, 1);
  EXPECT_FALSE(map.Erase(2));
  EXPECT_EQ(map.size(), 1u);
}

TEST(F14MapSetTest, EraseFromEmpty) {
  IntSet map;
  EXPECT_FALSE(map.Erase(1));
}

TEST(F14MapSetTest, Clear) {
  IntSet map;
  for (int i = 0; i < 50; ++i) {
    map.Insert(i, i);
  }
  EXPECT_EQ(map.size(), 50u);

  map.Clear();
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.Find(0), nullptr);
  EXPECT_EQ(map.Find(25), nullptr);
}

TEST(F14MapSetTest, ClearEmpty) {
  IntSet map;
  map.Clear();
  EXPECT_EQ(map.size(), 0u);
}

TEST(F14MapSetTest, InsertAfterClear) {
  IntSet map;
  map.Insert(1, 1);
  map.Clear();
  auto [val, inserted] = map.Insert(1, 1);
  ASSERT_NE(val, nullptr);
  EXPECT_TRUE(inserted);
  EXPECT_EQ(*val, 1);
  EXPECT_EQ(map.size(), 1u);
}

TEST(F14MapSetTest, ForEachVisitsAll) {
  IntSet map;
  for (int i = 0; i < 20; ++i) {
    map.Insert(i, i);
  }

  std::size_t count = 0;
  map.ForEach([&](const int& key, int& mapped) -> bool {
    EXPECT_EQ(key, mapped);  // set mode: key == mapped
    ++count;
    return false;
  });
  EXPECT_EQ(count, 20u);
  EXPECT_EQ(map.size(), 20u);
}

TEST(F14MapSetTest, ForEachWithErasure) {
  IntSet map;
  for (int i = 0; i < 20; ++i) {
    map.Insert(i, i);
  }

  // Erase even keys
  map.ForEach([](const int& key, int& /*mapped*/) -> bool {
    return key % 2 == 0;
  });

  EXPECT_EQ(map.size(), 10u);
  for (int i = 0; i < 20; ++i) {
    if (i % 2 == 0) {
      EXPECT_EQ(map.Find(i), nullptr) << "key=" << i;
    } else {
      ASSERT_NE(map.Find(i), nullptr) << "key=" << i;
      EXPECT_EQ(*map.Find(i), i);
    }
  }
}

TEST(F14MapSetTest, ManyInserts) {
  IntSet map;
  constexpr int N = 1000;
  for (int i = 0; i < N; ++i) {
    auto [val, inserted] = map.Insert(i, i);
    ASSERT_NE(val, nullptr) << "i=" << i;
    EXPECT_TRUE(inserted) << "i=" << i;
  }
  EXPECT_EQ(map.size(), static_cast<std::size_t>(N));

  for (int i = 0; i < N; ++i) {
    int* found = map.Find(i);
    ASSERT_NE(found, nullptr) << "i=" << i;
    EXPECT_EQ(*found, i) << "i=" << i;
  }
}

TEST(F14MapSetTest, InsertEraseInsertCycle) {
  IntSet map;
  for (int i = 0; i < 100; ++i) {
    map.Insert(i, i);
  }
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(map.Erase(i));
  }
  EXPECT_EQ(map.size(), 0u);

  for (int i = 0; i < 100; ++i) {
    auto [val, inserted] = map.Insert(i, i);
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(inserted);
  }
  EXPECT_EQ(map.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    ASSERT_NE(map.Find(i), nullptr);
    EXPECT_EQ(*map.Find(i), i);
  }
}

TEST(F14MapSetTest, InitialCapacity) {
  IntSet map(100);
  EXPECT_EQ(map.size(), 0u);
  for (int i = 0; i < 100; ++i) {
    map.Insert(i, i);
  }
  EXPECT_EQ(map.size(), 100u);
}

TEST(F14MapSetTest, ZeroInitialCapacity) {
  IntSet map(0);
  EXPECT_EQ(map.size(), 0u);
  EXPECT_EQ(map.Find(0), nullptr);
  auto [val, inserted] = map.Insert(0, 0);
  ASSERT_NE(val, nullptr);
  EXPECT_TRUE(inserted);
}

// ---------------------------------------------------------------------------
// F14Map basic operations — map mode (Key != Value, Item = std::pair)
// ---------------------------------------------------------------------------

TEST(F14MapMapTest, StringKeyIntValue) {
  F14Map<std::string, int> map;
  map.Insert("hello", 1);
  map.Insert("world", 2);
  EXPECT_EQ(map.size(), 2u);

  ASSERT_NE(map.Find("hello"), nullptr);
  EXPECT_EQ(*map.Find("hello"), 1);
  ASSERT_NE(map.Find("world"), nullptr);
  EXPECT_EQ(*map.Find("world"), 2);
  EXPECT_EQ(map.Find("missing"), nullptr);

  EXPECT_TRUE(map.Erase("hello"));
  EXPECT_EQ(map.Find("hello"), nullptr);
  EXPECT_EQ(map.size(), 1u);
}

TEST(F14MapMapTest, IntKeyStringValue) {
  F14Map<int, std::string> map;
  auto [v1, i1] = map.Insert(1, "alpha");
  ASSERT_NE(v1, nullptr);
  EXPECT_TRUE(i1);
  EXPECT_EQ(*v1, "alpha");

  auto [v2, i2] = map.Insert(2, "beta");
  ASSERT_NE(v2, nullptr);
  EXPECT_TRUE(i2);

  // Duplicate returns existing
  auto [v3, i3] = map.Insert(1, "gamma");
  EXPECT_FALSE(i3);
  EXPECT_EQ(*v3, "alpha");

  EXPECT_EQ(map.size(), 2u);
  EXPECT_TRUE(map.Erase(1));
  EXPECT_EQ(map.Find(1), nullptr);
  EXPECT_EQ(map.size(), 1u);
}

TEST(F14MapMapTest, ForEachMapMode) {
  F14Map<int, std::string> map;
  map.Insert(1, "a");
  map.Insert(2, "b");
  map.Insert(3, "c");

  std::size_t count = 0;
  map.ForEach([&](const int& key, std::string& value) -> bool {
    EXPECT_FALSE(value.empty());
    ++count;
    return false;
  });
  EXPECT_EQ(count, 3u);
}

TEST(F14MapMapTest, ForEachWithErasureMapMode) {
  F14Map<int, std::string> map;
  for (int i = 0; i < 10; ++i) {
    map.Insert(i, std::to_string(i));
  }
  // Erase odd keys
  map.ForEach([](const int& key, std::string&) -> bool {
    return key % 2 == 1;
  });
  EXPECT_EQ(map.size(), 5u);
  for (int i = 0; i < 10; ++i) {
    if (i % 2 == 0) {
      ASSERT_NE(map.Find(i), nullptr);
      EXPECT_EQ(*map.Find(i), std::to_string(i));
    } else {
      EXPECT_EQ(map.Find(i), nullptr);
    }
  }
}

TEST(F14MapMapTest, ManyInsertsMapMode) {
  F14Map<int, std::string> map;
  constexpr int N = 200;
  for (int i = 0; i < N; ++i) {
    map.Insert(i, std::to_string(i * 3));
  }
  EXPECT_EQ(map.size(), static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) {
    auto* found = map.Find(i);
    ASSERT_NE(found, nullptr) << "i=" << i;
    EXPECT_EQ(*found, std::to_string(i * 3));
  }
}

// ---------------------------------------------------------------------------
// Compile-time mode checks
// ---------------------------------------------------------------------------

TEST(F14MapSetTest, EnableItemIterationFalse) {
  IntSet map;
  map.Insert(1, 1);
  map.Insert(2, 2);
  EXPECT_EQ(map.size(), 2u);
  ASSERT_NE(map.Find(1), nullptr);
  EXPECT_EQ(*map.Find(1), 1);
  EXPECT_TRUE(map.Erase(1));
  EXPECT_EQ(map.size(), 1u);
  map.Clear();
  EXPECT_EQ(map.size(), 0u);
}

TEST(F14MapSetTest, EnableItemIterationSizeDifference) {
  using MapTrue = F14Map<int64_t, int64_t, std::hash<int64_t>,
                         std::equal_to<int64_t>,
                         DefaultChunkAllocator, true>;
  using MapFalse = F14Map<int64_t, int64_t, std::hash<int64_t>,
                          std::equal_to<int64_t>,
                          DefaultChunkAllocator, false>;
  EXPECT_LT(sizeof(MapFalse), sizeof(MapTrue));
}

TEST(F14MapSetTest, SetModeItemIsSingleKey) {
  // In set mode (Key == Value), Item should be Key, not std::pair
  using SetMap = F14Map<int, int, std::hash<int>, std::equal_to<int>,
                        DefaultChunkAllocator, false>;
  static_assert(SetMap::kIsSetMode);
  static_assert(std::is_same_v<SetMap::Item, int>);
  static_assert(std::is_same_v<SetMap::Mapped, int>);
}

TEST(F14MapMapTest, MapModeItemIsPair) {
  // In map mode (Key != Value), Item should be std::pair<Key, Value>
  using MapMap = F14Map<int, std::string>;
  static_assert(!MapMap::kIsSetMode);
  static_assert((std::is_same_v<MapMap::Item, std::pair<int, std::string>>));
  static_assert(std::is_same_v<MapMap::Mapped, std::string>);
}

TEST(F14MapSetTest, ForEachOnEmpty) {
  IntSet map;
  std::size_t count = 0;
  map.ForEach([&](const int&, int&) -> bool {
    ++count;
    return false;
  });
  EXPECT_EQ(count, 0u);
}

TEST(F14MapSetTest, BeginEndOnEmpty) {
  IntSet map;
  EXPECT_EQ(map.Begin(), map.End());
}

// ---------------------------------------------------------------------------
// ItemIterator tests — set mode with int64_t (sizeof >= 8 for packed encoding)
// ---------------------------------------------------------------------------

using Int64Set = F14Map<int64_t, int64_t>;

TEST(F14MapIteratorTest, BeginEndEmptyAreEqual) {
  Int64Set map;
  EXPECT_EQ(map.Begin(), map.End());
  EXPECT_TRUE(map.Begin().AtEnd());
}

TEST(F14MapIteratorTest, IterateVisitsAllInsertedItems) {
  Int64Set map;
  constexpr int N = 50;
  for (int i = 0; i < N; ++i) {
    map.Insert(i, i);
  }

  std::size_t count = 0;
  std::set<int64_t> seen;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) {
    seen.insert(*it);
    ++count;
  }
  EXPECT_EQ(count, static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) {
    EXPECT_TRUE(seen.count(i)) << "missing key=" << i;
  }
}

TEST(F14MapIteratorTest, IterateAfterInsertAndErase) {
  Int64Set map;
  constexpr int N = 40;
  for (int i = 0; i < N; ++i) map.Insert(i, i);
  for (int i = 0; i < N; i += 2) EXPECT_TRUE(map.Erase(i));

  std::size_t count = 0;
  std::set<int64_t> seen;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) {
    seen.insert(*it);
    ++count;
  }
  EXPECT_EQ(count, map.size());
  for (int i = 0; i < N; ++i) {
    if (i % 2 == 1) EXPECT_TRUE(seen.count(i));
    else EXPECT_FALSE(seen.count(i));
  }
}

TEST(F14MapIteratorTest, AllAdvanceVariantsEquivalent) {
  Int64Set map;
  constexpr int N = 30;
  for (int i = 0; i < N; ++i) map.Insert(i, i);

  std::vector<int64_t> via_advance, via_likely_dead;
  for (auto it = map.Begin(); it != map.End(); it.Advance())
    via_advance.push_back(*it);
  for (auto it = map.Begin(); it != map.End(); it.AdvanceLikelyDead())
    via_likely_dead.push_back(*it);

  EXPECT_EQ(via_advance.size(), static_cast<std::size_t>(N));
  EXPECT_EQ(via_advance, via_likely_dead);
}

TEST(F14MapIteratorTest, SingleElement) {
  Int64Set map;
  map.Insert(42, 42);
  auto it = map.Begin();
  ASSERT_NE(it, map.End());
  EXPECT_EQ(*it, 42);
  it.Advance();
  EXPECT_EQ(it, map.End());
}

TEST(F14MapIteratorTest, IterateAfterClearAndReinsert) {
  Int64Set map;
  for (int i = 0; i < 20; ++i) map.Insert(i, i);
  map.Clear();
  EXPECT_EQ(map.Begin(), map.End());

  for (int i = 100; i < 105; ++i) map.Insert(i, i);
  std::size_t count = 0;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) {
    EXPECT_GE(*it, 100);
    EXPECT_LT(*it, 105);
    ++count;
  }
  EXPECT_EQ(count, 5u);
}

TEST(F14MapIteratorTest, ManyItemsIteration) {
  Int64Set map;
  constexpr int N = 500;
  for (int i = 0; i < N; ++i) map.Insert(i, i);

  std::size_t count = 0;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) ++count;
  EXPECT_EQ(count, static_cast<std::size_t>(N));
}

TEST(F14MapIteratorTest, DisabledIterationBeginEqualsEnd) {
  F14Map<int64_t, int64_t, std::hash<int64_t>, std::equal_to<int64_t>,
         DefaultChunkAllocator, false> map;
  for (int i = 0; i < 10; ++i) map.Insert(i, i);
  EXPECT_EQ(map.Begin(), map.End());
}

// ---------------------------------------------------------------------------
// ItemIterator tests — map mode
// ---------------------------------------------------------------------------

TEST(F14MapIteratorTest, MapModeIteration) {
  F14Map<int, std::string> map;
  map.Insert(1, "one");
  map.Insert(2, "two");
  map.Insert(3, "three");

  std::set<int> seen_keys;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) {
    seen_keys.insert(it->first);
    EXPECT_FALSE(it->second.empty());
  }
  EXPECT_EQ(seen_keys.size(), 3u);
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 4: Capacity computation produces valid
// power-of-two
// **Validates: Requirements 2.9**
// ---------------------------------------------------------------------------

// Replicate the Compute formula from F14Map (which is private) as a test
// helper so we can verify the property directly.
static void TestCompute(std::size_t desired, std::size_t* chunk_count,
                        std::size_t* scale) {
  std::size_t min_chunks = (desired - 1) / kDesiredCapacity + 1;
  std::size_t chunk_pow = 0;
  if (min_chunks > 1) {
    uint32_t mask = static_cast<uint32_t>(min_chunks - 1);
    chunk_pow = mask ? 1 + (31 ^ __builtin_clz(mask)) : 0;
  }
  *chunk_count = std::size_t{1} << chunk_pow;
  *scale = kDesiredCapacity;
}

RC_GTEST_PROP(F14MapProperty, CapacityComputationProducesValidPowerOfTwo,
              ()) {
  // Feature: f14-lookup-table, Property 4: Capacity computation produces
  // valid power-of-two
  auto desired =
      *rc::gen::inRange(static_cast<std::size_t>(1),
                        static_cast<std::size_t>(10001));

  std::size_t chunk_count = 0;
  std::size_t scale = 0;
  TestCompute(desired, &chunk_count, &scale);

  // chunk_count must be a power of two
  RC_ASSERT(chunk_count > static_cast<std::size_t>(0));
  RC_ASSERT((chunk_count & (chunk_count - 1)) == static_cast<std::size_t>(0));

  // chunk_count * kDesiredCapacity >= desired
  RC_ASSERT(chunk_count * static_cast<std::size_t>(kDesiredCapacity) >= desired);
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 5: F14Map model-based correctness
// **Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.6, 8.7, 8.9, 2.4**
// ---------------------------------------------------------------------------

enum class Op : uint8_t { Insert, Erase, Find };

RC_GTEST_PROP(F14MapProperty, ModelBasedCorrectness, ()) {
  // Feature: f14-lookup-table, Property 5: F14Map model-based correctness
  auto ops = *rc::gen::container<std::vector<std::pair<Op, int>>>(
      rc::gen::pair(
          rc::gen::element(Op::Insert, Op::Erase, Op::Find),
          rc::gen::inRange(0, 200)));

  F14Map<int, int, std::hash<int>, std::equal_to<int>,
         DefaultChunkAllocator, false> f14;
  std::unordered_map<int, int> ref;

  for (const auto& [op, key] : ops) {
    switch (op) {
      case Op::Insert: {
        auto [val, inserted] = f14.Insert(key, key);
        auto [it, ref_inserted] = ref.emplace(key, key);
        RC_ASSERT(inserted == ref_inserted);
        break;
      }
      case Op::Erase: {
        bool f14_erased = f14.Erase(key);
        bool ref_erased = ref.erase(key) > 0;
        RC_ASSERT(f14_erased == ref_erased);
        break;
      }
      case Op::Find: {
        int* found = f14.Find(key);
        auto it = ref.find(key);
        if (it == ref.end()) {
          RC_ASSERT(found == nullptr);
        } else {
          RC_ASSERT(found != nullptr);
          RC_ASSERT(*found == it->first);
        }
        break;
      }
    }

    // (a) sizes equal
    RC_ASSERT(f14.size() == ref.size());

    // (b) all reference keys found with correct value
    for (const auto& [rk, rv] : ref) {
      int* fv = f14.Find(rk);
      RC_ASSERT(fv != nullptr);
      RC_ASSERT(*fv == rk);
    }

    // (c) absent keys not found — spot check a few keys
    for (int probe = 200; probe < 210; ++probe) {
      if (ref.count(probe) == 0) {
        RC_ASSERT(f14.Find(probe) == nullptr);
      }
    }

    // (d) occupied slot sum equals size
    // (e) hosted overflow sum equals non-home item count
    if (f14.chunks() != nullptr) {
      std::size_t chunk_count = f14.chunk_mask() + 1;
      std::size_t occupied_sum = 0;
      std::size_t hosted_overflow_sum = 0;

      for (std::size_t ci = 0; ci < chunk_count; ++ci) {
        TagMask occ = f14.chunks()[ci].OccupiedMask();
        occupied_sum += __builtin_popcount(occ);
        hosted_overflow_sum += f14.chunks()[ci].HostedOverflowCount();
      }

      RC_ASSERT(occupied_sum == f14.size());

      // Count non-home items: items whose home chunk differs from actual chunk
      std::size_t non_home_count = 0;
      for (std::size_t ci = 0; ci < chunk_count; ++ci) {
        TagMask occ = f14.chunks()[ci].OccupiedMask();
        while (occ) {
          int idx = __builtin_ctz(occ);
          occ &= occ - 1;
          // In set mode, item IS the key
          auto& item = f14.chunks()[ci].items[idx];
          auto hp = SplitHash(std::hash<int>{}(item));
          std::size_t home = hp.hash & f14.chunk_mask();
          if (home != ci) {
            ++non_home_count;
          }
        }
      }
      RC_ASSERT(hosted_overflow_sum == non_home_count);
    }
  }
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 9: PackedPtr round-trip encoding
// **Validates: Requirements 9.3**
// ---------------------------------------------------------------------------

RC_GTEST_PROP(F14MapProperty, PackedPtrRoundTripEncoding, ()) {
  // Feature: f14-lookup-table, Property 9: PackedPtr round-trip encoding
  auto index = *rc::gen::inRange(static_cast<std::size_t>(0),
                                 static_cast<std::size_t>(14));

  // Use a Chunk with a concrete item type to get properly aligned item
  // pointers. We use uint64_t (same size as void*) to avoid void* show issues.
  alignas(128) Chunk<uint64_t> chunk;
  chunk.Clear();

  void* item_ptr = static_cast<void*>(&chunk.items[index]);
  PackedPtr pp = PackedFromItemPtr(item_ptr, index);

  // Decode using the same logic as Begin():
  //   encoded = (raw & 0x7) << 1
  //   deduced = (raw >> 3) & 0x1
  //   index   = encoded | deduced
  //   item_ptr = raw & ~0x7
  std::uintptr_t raw = pp.raw;
  std::uintptr_t encoded = (raw & 0x7) << 1;
  std::uintptr_t deduced = (raw >> 3) & 0x1;
  std::size_t decoded_index = static_cast<std::size_t>(encoded | deduced);
  std::uintptr_t decoded_addr = raw & ~std::uintptr_t{0x7};
  std::uintptr_t original_addr = reinterpret_cast<std::uintptr_t>(item_ptr);

  RC_ASSERT(decoded_addr == original_addr);
  RC_ASSERT(decoded_index == index);
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 10: Item iteration visits exactly
// size() items
// **Validates: Requirements 9.2, 9.4, 9.5, 9.10**
// ---------------------------------------------------------------------------

RC_GTEST_PROP(F14MapProperty, ItemIterationVisitsExactlySizeItems, ()) {
  // Feature: f14-lookup-table, Property 10: Item iteration visits exactly
  // size() items
  auto ops = *rc::gen::container<std::vector<std::pair<bool, int>>>(
      rc::gen::pair(rc::gen::arbitrary<bool>(),
                    rc::gen::inRange(0, 200)));

  F14Map<int64_t, int64_t, std::hash<int64_t>, std::equal_to<int64_t>,
         DefaultChunkAllocator, true> map;

  // Apply random insert/erase sequence
  for (const auto& [is_insert, key] : ops) {
    if (is_insert) {
      map.Insert(static_cast<int64_t>(key), static_cast<int64_t>(key));
    } else {
      map.Erase(static_cast<int64_t>(key));
    }
  }

  // Collect items via Advance()
  std::set<int64_t> keys_advance;
  std::vector<int64_t> items_advance;
  for (auto it = map.Begin(); it != map.End(); it.Advance()) {
    items_advance.push_back(*it);
    keys_advance.insert(*it);
  }

  // Exactly size() items visited, each once
  RC_ASSERT(items_advance.size() == map.size());
  RC_ASSERT(keys_advance.size() == map.size());

  // Collect items via AdvanceLikelyDead()
  std::vector<int64_t> items_likely_dead;
  for (auto it = map.Begin(); it != map.End(); it.AdvanceLikelyDead()) {
    items_likely_dead.push_back(*it);
  }

  // All three advance variants produce equivalent traversals
  RC_ASSERT(items_advance == items_likely_dead);

  // Verify each iterated key is findable
  for (int64_t k : items_advance) {
    auto* found = map.Find(k);
    RC_ASSERT(found != nullptr);
    RC_ASSERT(*found == k);
  }
}

}  // namespace
}  // namespace rxtx::f14
