// rxtx/test_utils.h
// Test utilities for Packet and Batch tests.
// Provides DPDK EAL initialization and a test mbuf allocator backed by a real
// DPDK mempool (using --no-huge mode for unit-test environments).
#ifndef RXTX_TEST_UTILS_H_
#define RXTX_TEST_UTILS_H_

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

namespace rxtx {
namespace testing {

// Initialize DPDK EAL once for the entire test process.
// Uses --no-huge and minimal log level so tests can run without hugepages.
inline bool InitEal() {
  static bool initialized = false;
  if (initialized) return true;

  // Minimal EAL arguments for test environments.
  const char* argv[] = {
      "test",
      "--no-huge",
      "--no-pci",
      "--log-level=1",
  };
  int argc = sizeof(argv) / sizeof(argv[0]);

  int ret = rte_eal_init(argc, const_cast<char**>(argv));
  if (ret < 0) return false;

  initialized = true;
  return true;
}

// A test allocator that wraps a real DPDK mempool.
// Tracks alloc/free counts by querying the mempool's available count.
class TestMbufAllocator {
 public:
  // Create a test mempool with the given number of mbufs.
  // Each mbuf has the default DPDK data room size.
  explicit TestMbufAllocator(unsigned count = 63,
                             uint16_t data_room_size = RTE_MBUF_DEFAULT_DATAROOM +
                                                       RTE_PKTMBUF_HEADROOM)
      : alloc_count_(0), free_count_(0) {
    static int pool_id = 0;
    char name[32];
    snprintf(name, sizeof(name), "test_pool_%d", pool_id++);

    pool_ = rte_pktmbuf_pool_create(name, count, 0, 0, data_room_size,
                                    rte_socket_id());
    if (pool_ == nullptr) {
      throw std::runtime_error("Failed to create test mempool");
    }
    initial_avail_ = rte_mempool_avail_count(pool_);
  }

  ~TestMbufAllocator() {
    rte_mempool_free(pool_);
  }

  // Non-copyable
  TestMbufAllocator(const TestMbufAllocator&) = delete;
  TestMbufAllocator& operator=(const TestMbufAllocator&) = delete;

  // Allocate an mbuf from the pool with configurable data_off and data_len.
  rte_mbuf* Alloc(uint16_t data_off = RTE_PKTMBUF_HEADROOM,
                  uint16_t data_len = 64) {
    rte_mbuf* m = rte_pktmbuf_alloc(pool_);
    if (m == nullptr) return nullptr;

    m->data_off = data_off;
    m->data_len = data_len;
    m->pkt_len = data_len;
    ++alloc_count_;
    return m;
  }

  // Number of mbufs currently allocated (i.e., not in the pool).
  unsigned InUseCount() const {
    return rte_mempool_in_use_count(pool_);
  }

  // Number of mbufs available in the pool.
  unsigned AvailCount() const {
    return rte_mempool_avail_count(pool_);
  }

  // Total allocations performed through this allocator.
  unsigned AllocCount() const { return alloc_count_; }

  // Access the underlying mempool (useful for advanced checks).
  rte_mempool* Pool() const { return pool_; }

 private:
  rte_mempool* pool_;
  unsigned initial_avail_;
  unsigned alloc_count_;
  unsigned free_count_;
};

}  // namespace testing
}  // namespace rxtx

#endif  // RXTX_TEST_UTILS_H_
