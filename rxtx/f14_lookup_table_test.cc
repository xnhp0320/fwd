// rxtx/f14_lookup_table_test.cc
// Property-based and unit tests for F14LookupTable.

#include "rxtx/f14_lookup_table.h"
#include "rxtx/fast_lookup_table.h"
#include "rxtx/lookup_entry.h"
#include "rxtx/packet_metadata.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

namespace rxtx {
namespace {

// Helper to construct an IPv4 IpAddress from a uint32_t.
IpAddress MakeIpv4(uint32_t addr) {
  IpAddress ip{};
  ip.v4 = addr;
  return ip;
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 6: F14LookupTable insert-find round-trip
// **Validates: Requirements 4.1, 4.2, 4.3, 8.2**
// ---------------------------------------------------------------------------

RC_GTEST_PROP(F14LookupTableProperty, InsertFindRoundTrip, ()) {
  // Feature: f14-lookup-table, Property 6: F14LookupTable insert-find round-trip
  auto src_v4 = *rc::gen::arbitrary<uint32_t>();
  auto dst_v4 = *rc::gen::arbitrary<uint32_t>();
  auto src_port = *rc::gen::arbitrary<uint16_t>();
  auto dst_port = *rc::gen::arbitrary<uint16_t>();
  auto protocol = *rc::gen::arbitrary<uint8_t>();
  auto vni = *rc::gen::arbitrary<uint32_t>();
  // flags: only bit 0 (IPv6) is meaningful; keep IPv4 for simplicity
  uint8_t flags = 0;

  F14LookupTable<> table(64);

  auto src = MakeIpv4(src_v4);
  auto dst = MakeIpv4(dst_v4);

  LookupEntry* inserted = table.Insert(src, dst, src_port, dst_port,
                                        protocol, vni, flags);
  RC_ASSERT(inserted != nullptr);
  RC_ASSERT(table.size() == static_cast<std::size_t>(1));

  // Verify fields match
  RC_ASSERT(inserted->src_ip.v4 == src_v4);
  RC_ASSERT(inserted->dst_ip.v4 == dst_v4);
  RC_ASSERT(inserted->src_port == src_port);
  RC_ASSERT(inserted->dst_port == dst_port);
  RC_ASSERT(inserted->protocol == protocol);
  RC_ASSERT(inserted->vni == vni);
  RC_ASSERT(inserted->flags == flags);

  // Find overload 1: individual fields
  LookupEntry* found1 = table.Find(src, dst, src_port, dst_port,
                                     protocol, vni, flags);
  RC_ASSERT(found1 != nullptr);
  RC_ASSERT(found1 == inserted);

  // Find overload 2: PacketMetadata
  PacketMetadata meta{};
  meta.src_ip = src;
  meta.dst_ip = dst;
  meta.src_port = src_port;
  meta.dst_port = dst_port;
  meta.protocol = protocol;
  meta.vni = vni;
  meta.flags = flags;  // IPv4

  LookupEntry* found2 = table.Find(meta);
  RC_ASSERT(found2 != nullptr);
  RC_ASSERT(found2 == inserted);

  F14LookupTable<>::PrefetchContext ctx{};
  table.Prefetch(meta, ctx);
  LookupEntry* found3 = table.FindWithPrefetch(meta, ctx);
  RC_ASSERT(found3 != nullptr);
  RC_ASSERT(found3 == inserted);
}

// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 7: F14LookupTable LRU promotion on
// Find hit
// **Validates: Requirements 4.7**
// ---------------------------------------------------------------------------

RC_GTEST_PROP(F14LookupTableProperty, LruPromotionOnFindHit, ()) {
  // Feature: f14-lookup-table, Property 7: F14LookupTable LRU promotion on Find hit
  auto n = *rc::gen::inRange(2, 33);  // 2..32 entries

  F14LookupTable<> table(static_cast<std::size_t>(n));

  // Insert N entries with distinct keys (different src_ip.v4 values)
  for (int i = 0; i < n; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    RC_ASSERT(e != nullptr);
  }
  RC_ASSERT(table.size() == static_cast<std::size_t>(n));

  // Pick a random entry to Find (promotes it to LRU tail)
  auto target_idx = *rc::gen::inRange(0, n);
  uint32_t target_ip = static_cast<uint32_t>(target_idx + 1);

  LookupEntry* found = table.Find(MakeIpv4(target_ip), MakeIpv4(0xFF),
                                   100, 200, 6, 10, 0);
  RC_ASSERT(found != nullptr);
  RC_ASSERT(found->src_ip.v4 == target_ip);

  // Evict (N-1) entries from LRU head. The found entry should survive
  // because it was promoted to the tail.
  std::size_t evicted = 0;
  for (int i = 0; i < n - 1; ++i) {
    std::size_t removed = table.EvictLru(1);
    RC_ASSERT(removed == static_cast<std::size_t>(1));
    evicted += removed;
  }
  RC_ASSERT(evicted == static_cast<std::size_t>(n - 1));
  RC_ASSERT(table.size() == static_cast<std::size_t>(1));

  // The last remaining entry should be the one we Found
  LookupEntry* survivor = table.Find(MakeIpv4(target_ip), MakeIpv4(0xFF),
                                      100, 200, 6, 10, 0);
  RC_ASSERT(survivor != nullptr);
  RC_ASSERT(survivor->src_ip.v4 == target_ip);

  // Evict the last one
  std::size_t final_evict = table.EvictLru(1);
  RC_ASSERT(final_evict == static_cast<std::size_t>(1));
  RC_ASSERT(table.size() == static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
// Feature: f14-lookup-table, Property 8: F14LookupTable behavioral
// equivalence with FastLookupTable
// **Validates: Requirements 8.8, 4.8, 4.11**
// ---------------------------------------------------------------------------

enum class TableOp : uint8_t { Insert, Find, Remove, EvictLru };

RC_GTEST_PROP(F14LookupTableProperty, BehavioralEquivalenceWithFastLookupTable,
              ()) {
  // Feature: f14-lookup-table, Property 8: F14LookupTable behavioral equivalence with FastLookupTable
  constexpr std::size_t kCapacity = 64;

  F14LookupTable<> f14(kCapacity);
  FastLookupTable<> fast(kCapacity);

  // Generate a sequence of operations
  auto ops = *rc::gen::container<std::vector<std::pair<TableOp, uint32_t>>>(
      rc::gen::pair(
          rc::gen::element(TableOp::Insert, TableOp::Find,
                           TableOp::Remove, TableOp::EvictLru),
          rc::gen::inRange<uint32_t>(1, 100)));

  // Track inserted keys so we can Remove by key (not by pointer)
  // We store the key (src_ip.v4) for entries currently in both tables.
  std::vector<uint32_t> inserted_keys;

  for (const auto& [op, val] : ops) {
    switch (op) {
      case TableOp::Insert: {
        auto src = MakeIpv4(val);
        auto dst = MakeIpv4(0xFF);
        LookupEntry* f14_result = f14.Insert(src, dst, 100, 200, 6, 10, 0);
        LookupEntry* fast_result = fast.Insert(src, dst, 100, 200, 6, 10, 0);

        // Both non-null or both null
        RC_ASSERT((f14_result != nullptr) == (fast_result != nullptr));

        if (f14_result != nullptr) {
          // Track the key if it's new
          bool already_tracked = false;
          for (auto k : inserted_keys) {
            if (k == val) { already_tracked = true; break; }
          }
          if (!already_tracked) {
            inserted_keys.push_back(val);
          }
        }
        break;
      }
      case TableOp::Find: {
        auto src = MakeIpv4(val);
        auto dst = MakeIpv4(0xFF);
        LookupEntry* f14_result = f14.Find(src, dst, 100, 200, 6, 10, 0);
        LookupEntry* fast_result = fast.Find(src, dst, 100, 200, 6, 10, 0);

        // Both non-null or both null
        RC_ASSERT((f14_result != nullptr) == (fast_result != nullptr));
        break;
      }
      case TableOp::Remove: {
        // Remove by key: find in both tables, then remove the pointers
        auto src = MakeIpv4(val);
        auto dst = MakeIpv4(0xFF);

        // We need to find the actual entry pointers in each table
        // Use Find (which also promotes LRU, but that's symmetric)
        LookupEntry* f14_entry = f14.Find(src, dst, 100, 200, 6, 10, 0);
        LookupEntry* fast_entry = fast.Find(src, dst, 100, 200, 6, 10, 0);

        RC_ASSERT((f14_entry != nullptr) == (fast_entry != nullptr));

        if (f14_entry != nullptr) {
          bool f14_removed = f14.Remove(f14_entry);
          bool fast_removed = fast.Remove(fast_entry);
          RC_ASSERT(f14_removed == fast_removed);

          // Remove from tracking
          inserted_keys.erase(
              std::remove(inserted_keys.begin(), inserted_keys.end(), val),
              inserted_keys.end());
        }
        break;
      }
      case TableOp::EvictLru: {
        std::size_t batch = static_cast<std::size_t>(val % 5) + 1;
        std::size_t f14_evicted = f14.EvictLru(batch);
        std::size_t fast_evicted = fast.EvictLru(batch);
        RC_ASSERT(f14_evicted == fast_evicted);

        // Rebuild inserted_keys from scratch by probing both tables
        // (eviction removes unknown entries from our tracking)
        std::vector<uint32_t> still_present;
        for (auto k : inserted_keys) {
          auto src = MakeIpv4(k);
          auto dst = MakeIpv4(0xFF);
          LookupEntry* f14_check = f14.Find(src, dst, 100, 200, 6, 10, 0);
          if (f14_check != nullptr) {
            still_present.push_back(k);
          }
        }
        inserted_keys = std::move(still_present);
        break;
      }
    }

    // After each operation: sizes must match
    RC_ASSERT(f14.size() == fast.size());
  }
}

// ---------------------------------------------------------------------------
// Unit tests for edge cases (Task 4.5)
// ---------------------------------------------------------------------------

// Slab exhaustion: fill to capacity, verify next Insert returns nullptr
TEST(F14LookupTableTest, SlabExhaustionReturnsNullptr) {
  constexpr std::size_t kCapacity = 4;
  F14LookupTable<> table(kCapacity);

  for (std::size_t i = 0; i < kCapacity; ++i) {
    auto* entry = table.Insert(
        MakeIpv4(static_cast<uint32_t>(i + 1)), MakeIpv4(0xFF),
        100, 200, 6, 10, 0);
    ASSERT_NE(entry, nullptr) << "Insert failed at i=" << i;
  }
  EXPECT_EQ(table.size(), kCapacity);

  // Next insert with a new key should fail
  auto* overflow = table.Insert(
      MakeIpv4(0xDEAD), MakeIpv4(0xFF), 100, 200, 6, 10, 0);
  EXPECT_EQ(overflow, nullptr);
  EXPECT_EQ(table.size(), kCapacity);
}

// Modifiable flag: Insert/Remove blocked when false, unblocked when re-enabled
TEST(F14LookupTableTest, InsertBlockedWhenNotModifiable) {
  F14LookupTable<> table(8);
  table.SetModifiable(false);

  auto* result = table.Insert(MakeIpv4(0x0A000001), MakeIpv4(0x0A000002),
                               12345, 80, 6, 100, 0);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(table.size(), 0u);
}

TEST(F14LookupTableTest, RemoveBlockedWhenNotModifiable) {
  F14LookupTable<> table(8);
  auto src = MakeIpv4(0x0A000001);
  auto dst = MakeIpv4(0x0A000002);

  LookupEntry* entry = table.Insert(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(table.size(), 1u);

  table.SetModifiable(false);
  EXPECT_FALSE(table.Remove(entry));
  EXPECT_EQ(table.size(), 1u);
}

TEST(F14LookupTableTest, ReEnableModifications) {
  F14LookupTable<> table(8);
  auto src = MakeIpv4(0x0A000001);
  auto dst = MakeIpv4(0x0A000002);

  table.SetModifiable(false);
  table.SetModifiable(true);

  LookupEntry* entry = table.Insert(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(table.size(), 1u);

  EXPECT_TRUE(table.Remove(entry));
  EXPECT_EQ(table.size(), 0u);
}

// Empty table: Find returns nullptr, Remove returns false, EvictLru returns 0
TEST(F14LookupTableTest, FindOnEmptyTableReturnsNullptr) {
  F14LookupTable<> table(8);
  auto* result = table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 42, 0);
  EXPECT_EQ(result, nullptr);
}

TEST(F14LookupTableTest, FindMetadataOnEmptyTableReturnsNullptr) {
  F14LookupTable<> table(8);
  PacketMetadata meta{};
  meta.src_ip = MakeIpv4(1);
  meta.dst_ip = MakeIpv4(2);
  meta.src_port = 100;
  meta.dst_port = 200;
  meta.protocol = 6;
  meta.vni = 42;
  meta.flags = 0;
  EXPECT_EQ(table.Find(meta), nullptr);
}

TEST(F14LookupTableTest, RemoveOnEmptyTableReturnsFalse) {
  F14LookupTable<> table(8);
  LookupEntry dummy{};
  EXPECT_FALSE(table.Remove(&dummy));
}

TEST(F14LookupTableTest, EvictLruOnEmptyTableReturnsZero) {
  F14LookupTable<> table(8);
  EXPECT_EQ(table.EvictLru(10), 0u);
}

// Duplicate insert: same pointer returned, size unchanged
TEST(F14LookupTableTest, DuplicateInsertReturnsSamePointer) {
  F14LookupTable<> table(8);
  auto src = MakeIpv4(0xC0A80001);
  auto dst = MakeIpv4(0xC0A80002);

  LookupEntry* first = table.Insert(src, dst, 5000, 443, 17, 42, 0);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(table.size(), 1u);

  LookupEntry* second = table.Insert(src, dst, 5000, 443, 17, 42, 0);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second, first);
  EXPECT_EQ(table.size(), 1u);
}

// Overflow saturation: increment from 255 stays at 255, decrement from 255
// stays at 255 (already tested in f14_map_test.cc, included here for
// completeness)
TEST(F14LookupTableTest, OverflowSaturation) {
  alignas(128) f14::Chunk<void*> chunk;
  chunk.Clear();

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

// EnableItemIteration=false compiled-out check (already tested in
// f14_map_test.cc, included here for completeness)
TEST(F14LookupTableTest, EnableItemIterationSizeDifference) {
  using MapTrue = f14::F14Map<int64_t, int64_t, std::hash<int64_t>,
                              std::equal_to<int64_t>,
                              f14::DefaultChunkAllocator, true>;
  using MapFalse = f14::F14Map<int64_t, int64_t, std::hash<int64_t>,
                               std::equal_to<int64_t>,
                               f14::DefaultChunkAllocator, false>;
  EXPECT_LT(sizeof(MapFalse), sizeof(MapTrue));
}

// Begin()/End() on empty map with EnableItemIteration=true (already tested
// in f14_map_test.cc, included here for completeness)
TEST(F14LookupTableTest, BeginEndOnEmptyMapWithItemIteration) {
  f14::F14Map<int64_t, int64_t> map;
  EXPECT_EQ(map.Begin(), map.End());
}

}  // namespace
}  // namespace rxtx
