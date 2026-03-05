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

}  // namespace
}  // namespace rxtx

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
