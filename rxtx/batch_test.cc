// rxtx/batch_test.cc
// Smoke tests for the Batch class template.
// Property-based and detailed unit tests will be added in later tasks.

#include "rxtx/batch.h"
#include "rxtx/test_utils.h"

#include <gtest/gtest.h>

class BatchTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }
};

TEST_F(BatchTest, EmptyBatchHasZeroCountAndCorrectCapacity) {
  rxtx::Batch<16> batch;
  EXPECT_EQ(batch.Count(), 0);
  EXPECT_EQ(batch.Capacity(), 16);
}

TEST_F(BatchTest, CapacityMatchesTemplateParameter) {
  EXPECT_EQ((rxtx::Batch<32>::Capacity()), 32);
  EXPECT_EQ((rxtx::Batch<64>::Capacity()), 64);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
