#ifndef CONFIG_PMD_THREAD_MANAGER_H_
#define CONFIG_PMD_THREAD_MANAGER_H_

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "config/pmd_thread.h"

namespace rcu {
class RcuManager;
}  // namespace rcu

namespace dpdk_config {

// Manages lifecycle and coordination of all PMD threads
// Mirrors the PortManager/DpdkPort architecture pattern
class PMDThreadManager {
 public:
  PMDThreadManager() = default;

  // Set the RCU manager. When set, LaunchThreads will register threads
  // and the hot loop will report quiescent states.
  // When nullptr (default), PMDThreadManager operates without RCU support.
  void SetRcuManager(rcu::RcuManager* rcu_manager);

  // Initialize and launch all PMD threads from configuration
  // Must be called after rte_eal_init()
  // Skips the main lcore (reserved for control plane)
  absl::Status LaunchThreads(const std::vector<PmdThreadConfig>& thread_configs,
                              bool verbose = false);

  // Stop all running PMD threads
  void StopAllThreads();

  // Wait for all PMD threads to complete
  // Calls rte_eal_wait_lcore for each launched thread
  absl::Status WaitForThreads();

  // Get a specific thread by lcore ID
  // Returns nullptr if lcore_id not found
  PmdThread* GetThread(uint32_t lcore_id);

  // Get all lcore IDs with running threads
  std::vector<uint32_t> GetLcoreIds() const;

  // Get number of launched threads
  size_t GetThreadCount() const { return threads_.size(); }

 private:
  // Global flag to signal threads to stop
  std::atomic<bool> stop_flag_{false};

  // Optional RCU manager for thread registration and QSBR support. Not owned.
  rcu::RcuManager* rcu_manager_ = nullptr;

  // Map of lcore_id to PMDThread instances
  std::unordered_map<uint32_t, std::unique_ptr<PmdThread>> threads_;
};

}  // namespace dpdk_config

#endif  // CONFIG_PMD_THREAD_MANAGER_H_
