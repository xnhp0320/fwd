// rxtx/packet_test.cc
// Smoke tests for the Packet class.
// Property-based and detailed unit tests will be added in later tasks.

#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"
#include "rxtx/test_utils.h"

#include <gtest/gtest.h>

class PacketTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }
};

TEST_F(PacketTest, FromReturnsSameAddress) {
  rxtx::testing::TestMbufAllocator alloc;
  rte_mbuf* m = alloc.Alloc();
  ASSERT_NE(m, nullptr);

  rxtx::Packet& pkt = rxtx::Packet::from(m);

  // Packet::from must return a reference at the same address as the mbuf.
  EXPECT_EQ(reinterpret_cast<void*>(&pkt), reinterpret_cast<void*>(m));
  EXPECT_EQ(pkt.Mbuf(), m);

  // Clean up — free the mbuf back to the pool.
  pkt.Free();
}

TEST_F(PacketTest, MetadataAccessorReturnsCorrectOffset) {
  rxtx::testing::TestMbufAllocator alloc;
  rte_mbuf* m = alloc.Alloc();
  ASSERT_NE(m, nullptr);

  rxtx::Packet& pkt = rxtx::Packet::from(m);

  // Metadata must live immediately after the rte_mbuf, at offset kMbufStructSize.
  auto* expected = reinterpret_cast<rxtx::PacketMetadata*>(
      reinterpret_cast<char*>(m) + rxtx::kMbufStructSize);
  EXPECT_EQ(&pkt.Metadata(), expected);

  pkt.Free();
}

TEST_F(PacketTest, PrefetchDoesNotCrash) {
  rxtx::testing::TestMbufAllocator alloc;
  rte_mbuf* m = alloc.Alloc();
  ASSERT_NE(m, nullptr);

  rxtx::Packet& pkt = rxtx::Packet::from(m);

  // Smoke test: calling Prefetch() should not crash.
  pkt.Prefetch();

  pkt.Free();
}

TEST_F(PacketTest, MetadataSizeMatchesPacketMetadata) {
  // Verify at runtime that kMetadataSize equals sizeof(PacketMetadata).
  // This complements the static_assert in packet.h.
  EXPECT_EQ(rxtx::kMetadataSize, sizeof(rxtx::PacketMetadata));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
