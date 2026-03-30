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

TEST_F(BatchTest, BatchResultClassifyKeepsFastAndMovesSlowWithResults) {
  constexpr uint16_t kSize = 16;
  rxtx::testing::TestMbufAllocator alloc(128);

  rxtx::Batch<kSize> main_batch;
  for (uint16_t len = 60; len < 66; ++len) {
    rte_mbuf* m = alloc.Alloc(RTE_PKTMBUF_HEADROOM, len);
    ASSERT_NE(m, nullptr);
    main_batch.Append(m);
  }

  rxtx::Batch<kSize> slow1_batch;
  rxtx::Batch<kSize> slow2_batch;
  rxtx::BatchResult<uint16_t, kSize> main_res(&main_batch);
  rxtx::BatchResult<uint16_t, kSize> slow1_res(&slow1_batch);
  rxtx::BatchResult<uint16_t, kSize> slow2_res(&slow2_batch);

  main_res.Build([](rxtx::Packet& pkt, uint16_t& out) { out = pkt.Length(); });

  std::array<rxtx::Batch<kSize>*, 2> slow_batches = {&slow1_batch, &slow2_batch};
  std::array<rxtx::BatchResult<uint16_t, kSize>*, 2> slow_results = {
      &slow1_res, &slow2_res};
  main_res.Classify<3>(
      [](rxtx::Packet&, uint16_t& out) -> std::size_t {
        if ((out % 3) == 0) return 0;  // fast path
        if ((out % 3) == 1) return 1;  // slow lane 1
        return 2;                      // slow lane 2
      },
      slow_batches, slow_results);

  EXPECT_EQ(main_batch.Count(), 2);   // 60, 63
  EXPECT_EQ(slow1_batch.Count(), 2);  // 61, 64
  EXPECT_EQ(slow2_batch.Count(), 2);  // 62, 65

  main_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 0);
  });
  slow1_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 1);
  });
  slow2_res.ForEach([](rxtx::Packet& pkt, uint16_t& out) {
    EXPECT_EQ(out, pkt.Length());
    EXPECT_EQ(out % 3, 2);
  });

  for (uint16_t i = 0; i < main_batch.Count(); ++i) {
    rte_pktmbuf_free(main_batch.Data()[i]);
  }
  for (uint16_t i = 0; i < slow1_batch.Count(); ++i) {
    rte_pktmbuf_free(slow1_batch.Data()[i]);
  }
  for (uint16_t i = 0; i < slow2_batch.Count(); ++i) {
    rte_pktmbuf_free(slow2_batch.Data()[i]);
  }
  main_batch.Release();
  slow1_batch.Release();
  slow2_batch.Release();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
