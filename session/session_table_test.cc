// session/session_table_test.cc
// Unit tests for SessionKey, SessionEntry, and SessionTable.
// Requires DPDK EAL initialization for rte_hash and rte_mempool.

#include "session/session_table.h"

#include <cstring>

#include "gtest/gtest.h"
#include "rxtx/packet_metadata.h"
#include "session/session_entry.h"
#include "session/session_key.h"

#include "rxtx/test_utils.h"

namespace session {
namespace {

// ---------------------------------------------------------------------------
// SessionKey tests
// ---------------------------------------------------------------------------

class SessionKeyTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }
};

TEST_F(SessionKeyTest, FromMetadataPopulatesAllFields) {
  rxtx::PacketMetadata meta = {};
  meta.src_ip.v4 = 0x0A000001;  // 10.0.0.1
  meta.dst_ip.v4 = 0x0A000002;  // 10.0.0.2
  meta.src_port = 12345;
  meta.dst_port = 80;
  meta.protocol = 6;  // TCP
  meta.flags = 0;     // IPv4

  SessionKey key = SessionKey::FromMetadata(meta, /*zone_id=*/42);

  EXPECT_EQ(key.src_ip.v4, 0x0A000001u);
  EXPECT_EQ(key.dst_ip.v4, 0x0A000002u);
  EXPECT_EQ(key.src_port, 12345);
  EXPECT_EQ(key.dst_port, 80);
  EXPECT_EQ(key.zone_id, 42u);
  EXPECT_EQ(key.protocol, 6);
  EXPECT_EQ(key.flags, 0);
}

TEST_F(SessionKeyTest, PaddingBytesAreZeroed) {
  rxtx::PacketMetadata meta = {};
  meta.src_ip.v4 = 0xC0A80001;
  meta.dst_ip.v4 = 0xC0A80002;
  meta.src_port = 5000;
  meta.dst_port = 443;
  meta.protocol = 17;  // UDP
  meta.flags = 0;

  SessionKey key1 = SessionKey::FromMetadata(meta, 7);
  SessionKey key2 = SessionKey::FromMetadata(meta, 7);

  // Both keys built from the same metadata must be byte-identical,
  // which proves padding bytes are consistently zeroed.
  EXPECT_EQ(std::memcmp(&key1, &key2, sizeof(SessionKey)), 0);
  // Explicitly check padding bytes.
  EXPECT_EQ(key1.pad_[0], 0);
  EXPECT_EQ(key1.pad_[1], 0);
}

TEST_F(SessionKeyTest, Ipv6FlagPropagated) {
  rxtx::PacketMetadata meta = {};
  meta.flags = rxtx::kFlagIpv6;
  meta.protocol = 6;

  SessionKey key = SessionKey::FromMetadata(meta, 0);
  EXPECT_EQ(key.flags, static_cast<uint8_t>(rxtx::kFlagIpv6));
}

TEST_F(SessionKeyTest, NonIpv6FlagsNotCopied) {
  rxtx::PacketMetadata meta = {};
  // Set multiple flags — only kFlagIpv6 should be copied.
  meta.flags = rxtx::kFlagIpv6 | rxtx::kFlagFragment | rxtx::kFlagIpv4Options;
  meta.protocol = 17;

  SessionKey key = SessionKey::FromMetadata(meta, 1);
  EXPECT_EQ(key.flags, static_cast<uint8_t>(rxtx::kFlagIpv6));
}

// ---------------------------------------------------------------------------
// SessionTable tests
// ---------------------------------------------------------------------------

class SessionTableTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }

  void SetUp() override {
    table_ = std::make_unique<SessionTable>();
    // Use a unique name per test to avoid rte_hash/rte_mempool name collisions.
    static int test_id = 0;
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "st_test_%d_%d", getpid(), test_id++);
    // Capacity must be large enough for the hardcoded mempool cache_size (256)
    // in SessionTable::Init. rte_mempool requires cache_size < n/1.5.
    config_.capacity = 512;
    config_.name = name_buf;
    name_ = name_buf;
  }

  // Helper: build a SessionKey from simple values.
  SessionKey MakeKey(uint32_t src, uint32_t dst, uint16_t sport,
                     uint16_t dport, uint8_t proto = 6,
                     uint32_t zone = 0) {
    SessionKey key;
    std::memset(&key, 0, sizeof(key));
    key.src_ip.v4 = src;
    key.dst_ip.v4 = dst;
    key.src_port = sport;
    key.dst_port = dport;
    key.protocol = proto;
    key.zone_id = zone;
    key.flags = 0;
    return key;
  }

  std::unique_ptr<SessionTable> table_;
  SessionTable::Config config_;
  std::string name_;  // keep name alive for the duration of the test
};

// --- Init tests ---

TEST_F(SessionTableTest, InitWithValidCapacitySucceeds) {
  config_.name = name_.c_str();
  absl::Status status = table_->Init(config_, nullptr);
  ASSERT_TRUE(status.ok()) << "Init failed: " << status.message();
  EXPECT_EQ(table_->capacity(), 512u);
}

TEST_F(SessionTableTest, InitWithCapacityZeroReturnsError) {
  config_.capacity = 0;
  config_.name = name_.c_str();
  absl::Status status = table_->Init(config_, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(status));
}

TEST_F(SessionTableTest, InitWithNullptrQsbrSucceeds) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, /*qsbr_var=*/nullptr).ok());
}

// --- Insert tests ---

TEST_F(SessionTableTest, InsertReturnsNonNullWithVersion1) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(1, 2, 100, 200);
  SessionEntry* entry = table_->Insert(key);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->version.load(std::memory_order_relaxed), 1u);
}

TEST_F(SessionTableTest, InsertDuplicateReturnsSamePointer) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(1, 2, 100, 200);
  SessionEntry* first = table_->Insert(key);
  SessionEntry* second = table_->Insert(key);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, second);
}

// --- Lookup tests ---

TEST_F(SessionTableTest, LookupFindsExistingKey) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(10, 20, 300, 400);
  SessionEntry* inserted = table_->Insert(key);
  ASSERT_NE(inserted, nullptr);

  SessionEntry* found = table_->Lookup(key);
  EXPECT_EQ(found, inserted);
}

TEST_F(SessionTableTest, LookupReturnsNullptrForMissingKey) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(99, 99, 99, 99);
  EXPECT_EQ(table_->Lookup(key), nullptr);
}

// --- Delete tests ---

TEST_F(SessionTableTest, DeleteBumpsVersion) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(1, 2, 100, 200);
  SessionEntry* entry = table_->Insert(key);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->version.load(std::memory_order_relaxed), 1u);

  // Save pointer — after delete the entry is returned to pool but memory
  // is still valid (mempool doesn't unmap).
  SessionEntry* saved = entry;
  ASSERT_TRUE(table_->Delete(key).ok());
  EXPECT_EQ(saved->version.load(std::memory_order_relaxed), 2u);
}

TEST_F(SessionTableTest, DeleteMissingKeyReturnsNotFoundError) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(99, 99, 99, 99);
  absl::Status status = table_->Delete(key);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsNotFound(status));
}

TEST_F(SessionTableTest, LookupReturnsNullptrAfterDelete) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  SessionKey key = MakeKey(1, 2, 100, 200);
  ASSERT_NE(table_->Insert(key), nullptr);
  ASSERT_TRUE(table_->Delete(key).ok());
  EXPECT_EQ(table_->Lookup(key), nullptr);
}

// --- ForEach tests ---

TEST_F(SessionTableTest, ForEachVisitsAllEntries) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  // Insert 5 entries.
  for (uint32_t i = 0; i < 5; ++i) {
    ASSERT_NE(table_->Insert(MakeKey(i, i + 10, i, i + 10)), nullptr);
  }

  uint32_t visited = table_->ForEach(
      [](const SessionKey&, SessionEntry*) { return false; });
  EXPECT_EQ(visited, 5u);
}

TEST_F(SessionTableTest, ForEachWithDeletionRemovesEntries) {
  config_.name = name_.c_str();
  absl::Status status = table_->Init(config_, nullptr);
  ASSERT_TRUE(status.ok()) << "Init failed: " << status.message();

  // Insert 4 entries with distinct keys.
  SessionKey keys[4];
  for (uint32_t i = 0; i < 4; ++i) {
    keys[i] = MakeKey(i, i + 10, i, i + 10);
    ASSERT_NE(table_->Insert(keys[i]), nullptr);
  }

  // Delete all via ForEach.
  uint32_t visited = table_->ForEach(
      [](const SessionKey&, SessionEntry*) { return true; });
  EXPECT_EQ(visited, 4u);

  // After deletion, lookups should return nullptr.
  for (uint32_t i = 0; i < 4; ++i) {
    EXPECT_EQ(table_->Lookup(keys[i]), nullptr);
  }
}

TEST_F(SessionTableTest, ForEachOnEmptyReturns0) {
  config_.name = name_.c_str();
  ASSERT_TRUE(table_->Init(config_, nullptr).ok());

  uint32_t visited = table_->ForEach(
      [](const SessionKey&, SessionEntry*) { return false; });
  EXPECT_EQ(visited, 0u);
}

// --- Pool exhaustion test ---

TEST_F(SessionTableTest, PoolExhaustionReturnsNullptr) {
  // Use the smallest capacity that satisfies rte_mempool's cache_size
  // constraint (cache_size=256 requires n >= 256*1.5 = 384, round to 512).
  static int exhaust_id = 0;
  char exhaust_name[64];
  snprintf(exhaust_name, sizeof(exhaust_name), "st_exhaust_%d_%d",
           getpid(), exhaust_id++);

  SessionTable small_table;
  SessionTable::Config small_config;
  small_config.capacity = 512;
  small_config.name = exhaust_name;
  absl::Status status = small_table.Init(small_config, nullptr);
  ASSERT_TRUE(status.ok()) << "Init failed: " << status.message();

  // Insert until pool is full.
  int inserted = 0;
  for (uint32_t i = 0; i < 2048; ++i) {
    SessionEntry* entry = small_table.Insert(
        MakeKey(i, i + 10000, i, i + 10000));
    if (entry == nullptr) break;
    ++inserted;
  }

  // We should have inserted some entries and then hit the limit.
  EXPECT_GE(inserted, 1);
  // rte_mempool may round up capacity, but we should eventually exhaust it.
  EXPECT_LT(inserted, 2048);

  // The next insert after exhaustion should return nullptr.
  SessionEntry* overflow = small_table.Insert(
      MakeKey(9999, 9999, 9999, 9999));
  EXPECT_EQ(overflow, nullptr);
}

}  // namespace
}  // namespace session

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
