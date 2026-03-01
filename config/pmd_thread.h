#ifndef CONFIG_PMD_THREAD_H_
#define CONFIG_PMD_THREAD_H_

#include <atomic>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "config/dpdk_config.h"

struct rte_rcu_qsbr;

namespace dpdk_config {

// PMD thread worker function that runs on each lcore
// PMD thread worker function that runs on each lcore
class PmdThread {
 public:
  // Create a PMD thread from configuration with a reference to the stop flag.
  // qsbr_var is an optional QSBR variable pointer for RCU quiescent state
  // reporting; when nullptr, quiescent state reporting is skipped.
  PmdThread(const PmdThreadConfig& config, std::atomic<bool>* stop_flag,
            struct rte_rcu_qsbr* qsbr_var = nullptr);

  // Get the lcore ID this thread runs on
  uint32_t GetLcoreId() const { return config_.lcore_id; }

  // Get RX queue assignments
  const std::vector<QueueAssignment>& GetRxQueues() const {
    return config_.rx_queues;
  }

  // Get TX queue assignments
  const std::vector<QueueAssignment>& GetTxQueues() const {
    return config_.tx_queues;
  }

  // Static entry point for DPDK remote launch
  // This is the function passed to rte_eal_remote_launch
  static int RunStub(void* arg);

 private:
  // The actual packet processing loop (stub implementation)
  int Run();

  // Configuration for this thread
  PmdThreadConfig config_;

  // Pointer to the stop flag (owned by PMDThreadManager)
  std::atomic<bool>* stop_flag_ptr_;

  // Optional QSBR variable for RCU quiescent state reporting.
  // When non-null, Run() passes it to the launcher so the hot loop
  // calls rte_rcu_qsbr_quiescent() after each processing batch.
  struct rte_rcu_qsbr* qsbr_var_ = nullptr;
}
;

}  // namespace dpdk_config

#endif  // CONFIG_PMD_THREAD_H_
