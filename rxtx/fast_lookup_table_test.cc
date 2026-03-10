// rxtx/fast_lookup_table_test.cc
// Unit tests for LookupEntry, LookupEntryHash, and LookupEntryEq.
// This file will be expanded later with FastLookupTable tests.

#include "rxtx/fast_lookup_table.h"
#include "rxtx/lookup_entry.h"
#include "rxtx/packet_metadata.h"

#include <cstdint>

#include <gtest/gtest.h>

namespace rxtx {
namespace {

// --- FromMetadata test ---

TEST(LookupEntryTest, FromMetadataMapsAllFields) {
  PacketMetadata meta{};
  meta.src_ip.v4 = 0x0A000001;  // 10.0.0.1
  meta.dst_ip.v4 = 0x0A000002;  // 10.0.0.2
  meta.src_port = 12345;
  meta.dst_port = 80;
  meta.protocol = 6;  // TCP
  meta.vni = 100;
  meta.flags = 0;  // IPv4

  LookupEntry entry{};
  entry.FromMetadata(meta);

  EXPECT_EQ(entry.src_ip.v4, meta.src_ip.v4);
  EXPECT_EQ(entry.dst_ip.v4, meta.dst_ip.v4);
  EXPECT_EQ(entry.src_port, meta.src_port);
  EXPECT_EQ(entry.dst_port, meta.dst_port);
  EXPECT_EQ(entry.protocol, meta.protocol);
  EXPECT_EQ(entry.vni, meta.vni);
  EXPECT_EQ(entry.flags, static_cast<uint8_t>(meta.flags & kFlagIpv6));
  EXPECT_FALSE(entry.IsIpv6());
}

// --- Hash consistency test ---

TEST(LookupEntryHashTest, IdenticalKeyFieldsProduceSameHash) {
  LookupEntry a{};
  a.src_ip.v4 = 0xC0A80001;  // 192.168.0.1
  a.dst_ip.v4 = 0xC0A80002;  // 192.168.0.2
  a.src_port = 5000;
  a.dst_port = 443;
  a.protocol = 17;  // UDP
  a.vni = 42;
  a.flags = 0;  // IPv4

  LookupEntry b{};
  b.src_ip.v4 = 0xC0A80001;
  b.dst_ip.v4 = 0xC0A80002;
  b.src_port = 5000;
  b.dst_port = 443;
  b.protocol = 17;
  b.vni = 42;
  b.flags = 0;

  EXPECT_EQ(LookupEntryHash{}(&a), LookupEntryHash{}(&b));
}

// --- Equality test: different flags ---

TEST(LookupEntryEqTest, DifferentFlagsReturnsFalse) {
  LookupEntry a{};
  a.src_ip.v4 = 0x01020304;
  a.dst_ip.v4 = 0x05060708;
  a.src_port = 1000;
  a.dst_port = 2000;
  a.protocol = 6;
  a.vni = 10;
  a.flags = 0;  // IPv4

  LookupEntry b{};
  b.src_ip.v4 = 0x01020304;
  b.dst_ip.v4 = 0x05060708;
  b.src_port = 1000;
  b.dst_port = 2000;
  b.protocol = 6;
  b.vni = 10;
  b.flags = 1;  // IPv6

  EXPECT_FALSE(LookupEntryEq{}(&a, &b));
}

// --- New session fields default value tests ---

TEST(LookupEntryTest, SessionDefaultsToNullptr) {
  LookupEntry entry{};
  EXPECT_EQ(entry.session, nullptr);
}

TEST(LookupEntryTest, CachedVersionDefaultsToZero) {
  LookupEntry entry{};
  EXPECT_EQ(entry.cached_version, 0u);
}

// --- Hash/eq ignore session and cached_version ---

TEST(LookupEntryHashTest, DifferentSessionFieldsProduceSameHash) {
  LookupEntry a{};
  a.src_ip.v4 = 0xC0A80001;
  a.dst_ip.v4 = 0xC0A80002;
  a.src_port = 5000;
  a.dst_port = 443;
  a.protocol = 17;
  a.vni = 42;
  a.flags = 0;
  a.cached_version = 0;
  a.session = nullptr;

  LookupEntry b{};
  b.src_ip.v4 = 0xC0A80001;
  b.dst_ip.v4 = 0xC0A80002;
  b.src_port = 5000;
  b.dst_port = 443;
  b.protocol = 17;
  b.vni = 42;
  b.flags = 0;
  b.cached_version = 99;
  b.session = reinterpret_cast<void*>(0xDEADBEEF);

  EXPECT_EQ(LookupEntryHash{}(&a), LookupEntryHash{}(&b));
}

TEST(LookupEntryEqTest, DifferentSessionFieldsStillEqual) {
  LookupEntry a{};
  a.src_ip.v4 = 0x01020304;
  a.dst_ip.v4 = 0x05060708;
  a.src_port = 1000;
  a.dst_port = 2000;
  a.protocol = 6;
  a.vni = 10;
  a.flags = 0;
  a.cached_version = 0;
  a.session = nullptr;

  LookupEntry b{};
  b.src_ip.v4 = 0x01020304;
  b.dst_ip.v4 = 0x05060708;
  b.src_port = 1000;
  b.dst_port = 2000;
  b.protocol = 6;
  b.vni = 10;
  b.flags = 0;
  b.cached_version = 42;
  b.session = reinterpret_cast<void*>(0xCAFEBABE);

  EXPECT_TRUE(LookupEntryEq{}(&a, &b));
}

// --- Helper for FastLookupTable tests ---

IpAddress MakeIpv4(uint32_t addr) {
  IpAddress ip{};
  ip.v4 = addr;
  return ip;
}

// --- FastLookupTable unit tests ---

TEST(FastLookupTableTest, FindOnEmptyTableReturnsNullptr) {
  FastLookupTable<> table(8);
  auto* result = table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 42, 0);
  EXPECT_EQ(result, nullptr);
}

TEST(FastLookupTableTest, RemoveOnEmptyTableReturnsFalse) {
  FastLookupTable<> table(8);
  // Create a dummy entry on the stack — Remove should find nothing to erase.
  LookupEntry dummy{};
  EXPECT_FALSE(table.Remove(&dummy));
}

TEST(FastLookupTableTest, InsertThenFindReturnsMatchingEntry) {
  FastLookupTable<> table(8);
  auto src = MakeIpv4(0x0A000001);
  auto dst = MakeIpv4(0x0A000002);

  LookupEntry* inserted = table.Insert(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(inserted, nullptr);
  EXPECT_EQ(table.size(), 1u);

  LookupEntry* found = table.Find(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found, inserted);
  EXPECT_EQ(found->src_ip.v4, 0x0A000001u);
  EXPECT_EQ(found->dst_ip.v4, 0x0A000002u);
  EXPECT_EQ(found->src_port, 12345);
  EXPECT_EQ(found->dst_port, 80);
  EXPECT_EQ(found->protocol, 6);
  EXPECT_EQ(found->vni, 100u);
  EXPECT_EQ(found->flags, 0);
}

TEST(FastLookupTableTest, DuplicateInsertReturnsSamePointerSizeIncrementsOnce) {
  FastLookupTable<> table(8);
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

TEST(FastLookupTableTest, CapacityExhaustionReturnsNullptr) {
  constexpr std::size_t kCapacity = 4;
  FastLookupTable<> table(kCapacity);

  // Fill to capacity with distinct keys (different src IPs).
  for (std::size_t i = 0; i < kCapacity; ++i) {
    auto* entry = table.Insert(
        MakeIpv4(static_cast<uint32_t>(i + 1)), MakeIpv4(0xFF),
        100, 200, 6, 10, 0);
    ASSERT_NE(entry, nullptr) << "Insert failed at i=" << i;
  }
  EXPECT_EQ(table.size(), kCapacity);

  // Next insert with a new key should fail.
  auto* overflow = table.Insert(
      MakeIpv4(0xDEAD), MakeIpv4(0xFF), 100, 200, 6, 10, 0);
  EXPECT_EQ(overflow, nullptr);
  EXPECT_EQ(table.size(), kCapacity);
}

TEST(FastLookupTableTest, RemoveThenFindReturnsNullptr) {
  FastLookupTable<> table(8);
  auto src = MakeIpv4(0x01020304);
  auto dst = MakeIpv4(0x05060708);

  LookupEntry* entry = table.Insert(src, dst, 1000, 2000, 6, 10, 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(table.size(), 1u);

  EXPECT_TRUE(table.Remove(entry));
  EXPECT_EQ(table.size(), 0u);

  auto* found = table.Find(src, dst, 1000, 2000, 6, 10, 0);
  EXPECT_EQ(found, nullptr);
}

// --- Modification toggle unit tests ---

TEST(FastLookupTableTest, ModifiableDefaultsToTrue) {
  FastLookupTable<> table(8);
  EXPECT_TRUE(table.IsModifiable());
}

TEST(FastLookupTableTest, SetModifiableRoundTrip) {
  FastLookupTable<> table(8);
  table.SetModifiable(false);
  EXPECT_FALSE(table.IsModifiable());
  table.SetModifiable(true);
  EXPECT_TRUE(table.IsModifiable());
}

TEST(FastLookupTableTest, InsertBlockedWhenNotModifiable) {
  FastLookupTable<> table(8);
  table.SetModifiable(false);

  auto* result = table.Insert(MakeIpv4(0x0A000001), MakeIpv4(0x0A000002),
                               12345, 80, 6, 100, 0);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(table.size(), 0u);
}

TEST(FastLookupTableTest, RemoveBlockedWhenNotModifiable) {
  FastLookupTable<> table(8);
  auto src = MakeIpv4(0x0A000001);
  auto dst = MakeIpv4(0x0A000002);

  LookupEntry* entry = table.Insert(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(table.size(), 1u);

  table.SetModifiable(false);
  EXPECT_FALSE(table.Remove(entry));
  EXPECT_EQ(table.size(), 1u);
}

TEST(FastLookupTableTest, ReEnableModifications) {
  FastLookupTable<> table(8);
  auto src = MakeIpv4(0x0A000001);
  auto dst = MakeIpv4(0x0A000002);

  table.SetModifiable(false);
  table.SetModifiable(true);

  // Insert should work after re-enabling.
  LookupEntry* entry = table.Insert(src, dst, 12345, 80, 6, 100, 0);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(table.size(), 1u);

  // Remove should work after re-enabling.
  EXPECT_TRUE(table.Remove(entry));
  EXPECT_EQ(table.size(), 0u);
}

// --- Iteration API unit tests ---

TEST(FastLookupTableTest, ForEachOnEmptyTableReturns0) {
  FastLookupTable<> table(8);
  auto it = table.Begin();
  std::size_t visited = table.ForEach(it, 10, [](LookupEntry*) { return false; });
  EXPECT_EQ(visited, 0u);
  EXPECT_EQ(it, table.End());
}

TEST(FastLookupTableTest, ForEachWithCount0Returns0) {
  FastLookupTable<> table(8);
  table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  ASSERT_EQ(table.size(), 2u);

  auto it = table.Begin();
  auto original = it;
  std::size_t visited = table.ForEach(it, 0, [](LookupEntry*) { return false; });
  EXPECT_EQ(visited, 0u);
  EXPECT_EQ(it, original);
}

TEST(FastLookupTableTest, ForEachVisitsAllEntries) {
  constexpr std::size_t kN = 8;
  FastLookupTable<> table(kN);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  auto it = table.Begin();
  std::size_t visited = table.ForEach(it, kN, [](LookupEntry*) { return false; });
  EXPECT_EQ(visited, kN);
  EXPECT_EQ(it, table.End());
  EXPECT_EQ(table.size(), kN);  // nothing removed
}

TEST(FastLookupTableTest, ForEachPartialIteration) {
  constexpr std::size_t kN = 8;
  constexpr std::size_t kPartial = 3;
  FastLookupTable<> table(kN);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  auto it = table.Begin();
  std::size_t visited = table.ForEach(it, kPartial, [](LookupEntry*) { return false; });
  EXPECT_EQ(visited, kPartial);
  EXPECT_NE(it, table.End());
  EXPECT_EQ(table.size(), kN);  // nothing removed
}

TEST(FastLookupTableTest, ForEachWithRemoval) {
  constexpr std::size_t kN = 8;
  FastLookupTable<> table(kN);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  auto it = table.Begin();
  std::size_t visited = table.ForEach(it, kN, [](LookupEntry*) { return true; });
  EXPECT_EQ(visited, kN);
  EXPECT_EQ(table.size(), 0u);
  EXPECT_EQ(table.capacity(), kN);  // slab free_count restored
}

TEST(FastLookupTableTest, ForEachSelectiveRemoval) {
  constexpr std::size_t kN = 8;
  FastLookupTable<> table(kN);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  // Remove entries whose src_ip.v4 is even.
  std::size_t remove_count = 0;
  auto it = table.Begin();
  std::size_t visited = table.ForEach(it, kN, [&remove_count](LookupEntry* entry) {
    bool should_remove = (entry->src_ip.v4 % 2 == 0);
    if (should_remove) ++remove_count;
    return should_remove;
  });
  EXPECT_EQ(visited, kN);
  // src_ip values 1..8: even ones are 2, 4, 6, 8 → 4 removed
  EXPECT_EQ(remove_count, 4u);
  EXPECT_EQ(table.size(), kN - remove_count);
}

// --- LRU unit tests ---

// Property 4 invariant: Insert single entry → LRU list size equals 1.
// We verify by evicting 1 entry and confirming the table becomes empty.
TEST(FastLookupTableTest, InsertSingleEntryLruSizeEqualsOne) {
  FastLookupTable<> table(8);
  table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  ASSERT_EQ(table.size(), 1u);

  // Evict 1 from LRU — should remove exactly 1.
  std::size_t removed = table.EvictLru(1);
  EXPECT_EQ(removed, 1u);
  EXPECT_EQ(table.size(), 0u);
}

// Property 4 invariant: Insert N entries → LRU list size equals N.
TEST(FastLookupTableTest, InsertNEntriesLruSizeEqualsN) {
  constexpr std::size_t kN = 5;
  FastLookupTable<> table(16);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  // Evict all N — should remove exactly N.
  std::size_t removed = table.EvictLru(kN);
  EXPECT_EQ(removed, kN);
  EXPECT_EQ(table.size(), 0u);
}

// Property 4: Remove entry → LRU list size decreases.
TEST(FastLookupTableTest, RemoveEntryLruSizeDecreases) {
  FastLookupTable<> table(8);
  auto* e1 = table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  auto* e2 = table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  ASSERT_NE(e1, nullptr);
  ASSERT_NE(e2, nullptr);
  ASSERT_EQ(table.size(), 2u);

  EXPECT_TRUE(table.Remove(e1));
  EXPECT_EQ(table.size(), 1u);

  // Evict 1 — should remove the remaining entry.
  std::size_t removed = table.EvictLru(1);
  EXPECT_EQ(removed, 1u);
  EXPECT_EQ(table.size(), 0u);

  // Evict again — nothing left.
  removed = table.EvictLru(1);
  EXPECT_EQ(removed, 0u);
}

// Property 5: Find hit promotes entry to LRU tail.
// Insert A then B (LRU order: A, B). Find A → promotes A to tail (order: B, A).
// Evict 1 from head → should remove B.
TEST(FastLookupTableTest, FindHitPromotesToLruTail) {
  FastLookupTable<> table(8);
  auto* a = table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  auto* b = table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  // Find A → promotes A to tail. LRU order becomes: B (head), A (tail).
  auto* found = table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  EXPECT_EQ(found, a);

  // Evict 1 from head → should remove B (least recently used).
  table.EvictLru(1);
  EXPECT_EQ(table.size(), 1u);

  // A should still be findable.
  found = table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  EXPECT_EQ(found, a);

  // B should be gone.
  found = table.Find(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  EXPECT_EQ(found, nullptr);
}

// Property 6: Find miss does not modify LRU order.
// Insert A then B (LRU order: A, B). Find non-existent → order unchanged.
// Evict 1 → should remove A (head).
TEST(FastLookupTableTest, FindMissDoesNotModifyLruOrder) {
  FastLookupTable<> table(8);
  auto* a = table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  auto* b = table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  // Miss — should not change LRU order.
  auto* miss = table.Find(MakeIpv4(99), MakeIpv4(99), 999, 999, 6, 10, 0);
  EXPECT_EQ(miss, nullptr);

  // Evict 1 from head → should remove A (inserted first, still at head).
  table.EvictLru(1);
  EXPECT_EQ(table.size(), 1u);

  // A should be gone, B should remain.
  auto* found_a = table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  EXPECT_EQ(found_a, nullptr);
  auto* found_b = table.Find(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  EXPECT_EQ(found_b, b);
}

// EvictLru(0) returns 0, table unchanged.
TEST(FastLookupTableTest, EvictLruZeroBatchSizeReturnsZero) {
  FastLookupTable<> table(8);
  table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  ASSERT_EQ(table.size(), 1u);

  std::size_t removed = table.EvictLru(0);
  EXPECT_EQ(removed, 0u);
  EXPECT_EQ(table.size(), 1u);
}

// Property 8: EvictLru on empty table returns 0.
TEST(FastLookupTableTest, EvictLruOnEmptyTableReturnsZero) {
  FastLookupTable<> table(8);
  ASSERT_EQ(table.size(), 0u);

  std::size_t removed = table.EvictLru(10);
  EXPECT_EQ(removed, 0u);
  EXPECT_EQ(table.size(), 0u);
}

// Property 7: EvictLru(batch_size) removes from head in order.
// Insert A, B, C in order. Evict 2 → removes A, B. C remains.
TEST(FastLookupTableTest, EvictLruRemovesFromHeadInOrder) {
  FastLookupTable<> table(8);
  auto* a = table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  auto* b = table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  auto* c = table.Insert(MakeIpv4(5), MakeIpv4(6), 500, 600, 6, 30, 0);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(table.size(), 3u);

  std::size_t removed = table.EvictLru(2);
  EXPECT_EQ(removed, 2u);
  EXPECT_EQ(table.size(), 1u);

  // A and B (head entries) should be gone.
  EXPECT_EQ(table.Find(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0), nullptr);
  EXPECT_EQ(table.Find(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0), nullptr);

  // C (tail) should remain.
  auto* found_c = table.Find(MakeIpv4(5), MakeIpv4(6), 500, 600, 6, 30, 0);
  EXPECT_EQ(found_c, c);
}

// Property 8: EvictLru when table has fewer entries than batch_size removes all.
TEST(FastLookupTableTest, EvictLruFewerEntriesThanBatchSizeRemovesAll) {
  FastLookupTable<> table(8);
  table.Insert(MakeIpv4(1), MakeIpv4(2), 100, 200, 6, 10, 0);
  table.Insert(MakeIpv4(3), MakeIpv4(4), 300, 400, 17, 20, 0);
  ASSERT_EQ(table.size(), 2u);

  std::size_t removed = table.EvictLru(100);
  EXPECT_EQ(removed, 2u);
  EXPECT_EQ(table.size(), 0u);
}

// Property 4: ForEach with removal correctly unlinks LruNodes.
TEST(FastLookupTableTest, ForEachWithRemovalUnlinksLruNodes) {
  constexpr std::size_t kN = 4;
  FastLookupTable<> table(kN);
  for (std::size_t i = 0; i < kN; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }
  ASSERT_EQ(table.size(), kN);

  // Remove entries with even src_ip via ForEach.
  auto it = table.Begin();
  table.ForEach(it, kN, [](LookupEntry* entry) {
    return (entry->src_ip.v4 % 2 == 0);
  });
  // 2 even entries removed (src_ip 2, 4), 2 odd remain (src_ip 1, 3).
  EXPECT_EQ(table.size(), 2u);

  // LRU list should also have exactly 2 entries — evict all to verify.
  std::size_t removed = table.EvictLru(100);
  EXPECT_EQ(removed, 2u);
  EXPECT_EQ(table.size(), 0u);
}

// Slot index computation: insert entries sequentially, verify eviction order
// matches insertion order, confirming slot index tracking is correct.
TEST(FastLookupTableTest, SlotIndexComputationMatchesPointerArithmetic) {
  constexpr std::size_t kCapacity = 4;
  FastLookupTable<> table(kCapacity);

  for (std::size_t i = 0; i < kCapacity; ++i) {
    auto* e = table.Insert(MakeIpv4(static_cast<uint32_t>(i + 1)),
                           MakeIpv4(0xFF), 100, 200, 6, 10, 0);
    ASSERT_NE(e, nullptr);
  }

  // Evict one at a time — each eviction should remove the oldest entry.
  // This validates that slot_index correctly maps back to the right entry.
  table.EvictLru(1);
  EXPECT_EQ(table.size(), kCapacity - 1);
  EXPECT_EQ(table.Find(MakeIpv4(1), MakeIpv4(0xFF), 100, 200, 6, 10, 0),
            nullptr);

  table.EvictLru(1);
  EXPECT_EQ(table.size(), kCapacity - 2);
  EXPECT_EQ(table.Find(MakeIpv4(2), MakeIpv4(0xFF), 100, 200, 6, 10, 0),
            nullptr);

  // Remaining entries should still be findable.
  EXPECT_NE(table.Find(MakeIpv4(3), MakeIpv4(0xFF), 100, 200, 6, 10, 0),
            nullptr);
  EXPECT_NE(table.Find(MakeIpv4(4), MakeIpv4(0xFF), 100, 200, 6, 10, 0),
            nullptr);
}

// Requirement 2.4: sizeof(LookupEntry) == 64 static assertion holds.
TEST(FastLookupTableTest, LookupEntrySizeIs64Bytes) {
  static_assert(sizeof(LookupEntry) == 64,
                "LookupEntry must be exactly 64 bytes (one cache line)");
  EXPECT_EQ(sizeof(LookupEntry), 64u);
}

}  // namespace
}  // namespace rxtx

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
