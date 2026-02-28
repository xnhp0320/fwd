// rxtx/packet_test.cc
// Smoke tests for the Packet class.
// Property-based and detailed unit tests will be added in later tasks.

#include "rxtx/packet.h"
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
