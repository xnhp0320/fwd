// rxtx/batch_test.cc
// Smoke tests for the Batch class template.
// Property-based and detailed unit tests will be added in later tasks.

#include "rxtx/batch.h"
#include "rxtx/batch_result.h"
#include "rxtx/test_utils.h"

#include <array>
#include <vector>

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

TEST_F(BatchTest, BatchResultBuildAndForEachKeepsAlignment) {
  constexpr uint16_t kSize = 8;
  rxtx::testing::TestMbufAllocator alloc(64);
  rxtx::Batch<kSize> batch;
  std::array<uint16_t, 4> lens = {64, 80, 96, 112};

  for (uint16_t i = 0; i < lens.size(); ++i) {
    rte_mbuf* m = alloc.Alloc(RTE_PKTMBUF_HEADROOM, lens[i]);
    ASSERT_NE(m, nullptr);
    batch.Append(m);
  }

  rxtx::BatchResult<uint16_t, kSize> results(&batch);
  results.Build([](rxtx::Packet& pkt, uint16_t& out) { out = pkt.Length(); });

  std::vector<uint16_t> seen;
  results.ForEach([&](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    seen.push_back(out);
  });

  ASSERT_EQ(seen.size(), lens.size());
  for (std::size_t i = 0; i < lens.size(); ++i) {
    EXPECT_EQ(seen[i], lens[i]);
  }

  batch.Release();
  for (std::size_t i = 0; i < lens.size(); ++i) {
    rte_pktmbuf_free(batch.Data()[i]);
  }
}

TEST_F(BatchTest, BatchResultPrefetchForEachProcessesAllPackets) {
  constexpr uint16_t kSize = 8;
  rxtx::testing::TestMbufAllocator alloc(64);
  rxtx::Batch<kSize> batch;

  for (uint16_t i = 0; i < 5; ++i) {
    rte_mbuf* m = alloc.Alloc();
    ASSERT_NE(m, nullptr);
    batch.Append(m);
  }

  rxtx::BatchResult<uint32_t, kSize> results(&batch);
  results.Build([](rxtx::Packet&, uint32_t& out) { out = 1; });

  uint32_t sum = 0;
  results.PrefetchForEach<2>(
      [&](rxtx::Packet&, uint32_t& out) { sum += out; });
  EXPECT_EQ(sum, 5u);

  batch.Release();
  for (uint16_t i = 0; i < 5; ++i) {
    rte_pktmbuf_free(batch.Data()[i]);
  }
}

TEST_F(BatchTest, BatchResultPrefetchFilterSplitsKeepAndFailoverBatches) {
  constexpr uint16_t kSize = 16;
  rxtx::testing::TestMbufAllocator alloc(128);

  rxtx::Batch<kSize> main_batch;
  for (uint16_t len = 60; len < 66; ++len) {
    rte_mbuf* m = alloc.Alloc(RTE_PKTMBUF_HEADROOM, len);
    ASSERT_NE(m, nullptr);
    main_batch.Append(m);
  }

  rxtx::BatchResult<uint16_t, kSize> main_res(&main_batch);
  rxtx::Batch<kSize> fail_over_batch;

  main_res.PrefetchFilter<2>(
      [](rxtx::Packet* pkt, uint16_t& out,
         [[maybe_unused]] uint16_t idx) -> bool {
        out = pkt->Length();
        return (out % 2) == 0;
      },
      fail_over_batch);

  EXPECT_EQ(main_batch.Count(), 3);       // 60, 62, 64
  EXPECT_EQ(fail_over_batch.Count(), 3);  // 61, 63, 65

  main_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 2, 0);
  });

  for (uint16_t i = 0; i < main_batch.Count(); ++i) {
    rte_pktmbuf_free(main_batch.Data()[i]);
  }
  for (uint16_t i = 0; i < fail_over_batch.Count(); ++i) {
    rte_pktmbuf_free(fail_over_batch.Data()[i]);
  }
  main_batch.Release();
  fail_over_batch.Release();
}

TEST_F(BatchTest, FreeClassifyDistributesIntoOutputBatchResults) {
  constexpr uint16_t kSize = 16;
  rxtx::testing::TestMbufAllocator alloc(128);

  rxtx::Batch<kSize> input_batch;
  for (uint16_t len = 60; len < 66; ++len) {
    rte_mbuf* m = alloc.Alloc(RTE_PKTMBUF_HEADROOM, len);
    ASSERT_NE(m, nullptr);
    input_batch.Append(m);
  }

  rxtx::Batch<kSize> lane0_batch;
  rxtx::Batch<kSize> lane1_batch;
  rxtx::Batch<kSize> lane2_batch;
  rxtx::BatchResult<uint16_t, kSize> lane0_res(&lane0_batch);
  rxtx::BatchResult<uint16_t, kSize> lane1_res(&lane1_batch);
  rxtx::BatchResult<uint16_t, kSize> lane2_res(&lane2_batch);

  std::array<rxtx::BatchResult<uint16_t, kSize>*, 3> outputs = {
      &lane0_res, &lane1_res, &lane2_res};
  rxtx::Classify<uint16_t, 3>(
      input_batch,
      [](rxtx::Packet* pkt, uint16_t& out) -> std::size_t {
        out = pkt->Length();
        return static_cast<std::size_t>(out % 3);
      },
      outputs);

  EXPECT_EQ(input_batch.Count(), 0);
  EXPECT_EQ(lane0_batch.Count(), 2);  // 60, 63
  EXPECT_EQ(lane1_batch.Count(), 2);  // 61, 64
  EXPECT_EQ(lane2_batch.Count(), 2);  // 62, 65

  lane0_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 0);
  });
  lane1_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 1);
  });
  lane2_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 2);
  });

  for (uint16_t i = 0; i < lane0_batch.Count(); ++i) {
    rte_pktmbuf_free(lane0_batch.Data()[i]);
  }
  for (uint16_t i = 0; i < lane1_batch.Count(); ++i) {
    rte_pktmbuf_free(lane1_batch.Data()[i]);
  }
  for (uint16_t i = 0; i < lane2_batch.Count(); ++i) {
    rte_pktmbuf_free(lane2_batch.Data()[i]);
  }
  lane0_batch.Release();
  lane1_batch.Release();
  lane2_batch.Release();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
