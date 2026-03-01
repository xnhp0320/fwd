#include "config/pmd_thread_manager.h"

#include <iostream>
#include <rte_eal.h>
#include <rte_lcore.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"

namespace dpdk_config {

absl::Status PMDThreadManager::LaunchThreads(
    const std::vector<PmdThreadConfig>& thread_configs, bool verbose) {
  if (thread_configs.empty()) {
    return absl::OkStatus();  // No threads to launch
  }

  // Reset stop flag
  stop_flag_.store(false, std::memory_order_relaxed);

  // Clear any existing threads
  threads_.clear();

  unsigned main_lcore = rte_get_main_lcore();

  if (verbose) {
    std::cout << "Main lcore: " << main_lcore
              << " (reserved for control plane)\n";
    std::cout << "Launching " << thread_configs.size() << " PMD thread(s)\n";
  }

  // Launch PMD threads on configured lcores
  for (const auto& config : thread_configs) {
    // Skip main lcore - it's reserved for control plane
    if (config.lcore_id == main_lcore) {
      if (verbose) {
        std::cout << "Skipping lcore " << config.lcore_id
                  << " (main/control plane)\n";
      }
      continue;
    }

    if (verbose) {
      std::cout << "Launching PMD thread on lcore " << config.lcore_id << "\n";
      std::cout << "  RX queues: ";
      for (const auto& q : config.rx_queues) {
        std::cout << "(" << q.port_id << "," << q.queue_id << ") ";
      }
      std::cout << "\n  TX queues: ";
      for (const auto& q : config.tx_queues) {
        std::cout << "(" << q.port_id << "," << q.queue_id << ") ";
      }
      std::cout << "\n";
    }

    // Look up processor by name (or default) from the registry
    auto& registry = processor::ProcessorRegistry::Instance();

    std::string proc_name = config.processor_name.empty()
        ? processor::ProcessorRegistry::kDefaultProcessorName
        : config.processor_name;

    auto entry_or = registry.Lookup(proc_name);
    if (!entry_or.ok()) {
      return entry_or.status();
    }

    // Validate queue assignments against the processor's requirements
    auto check_status = (*entry_or)->checker(config.rx_queues, config.tx_queues);
    if (!check_status.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("PMD thread on lcore ", config.lcore_id,
                       ": processor '", proc_name, "' check failed: ",
                       check_status.message()));
    }

    // Create PMDThread instance
    auto thread = std::make_unique<PmdThread>(config, &stop_flag_);

    // Launch worker on the specified lcore
    int ret = rte_eal_remote_launch(PmdThread::RunStub, thread.get(),
                                     config.lcore_id);

    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("Failed to launch PMD thread on lcore ",
                       config.lcore_id));
    }

    // Store thread instance in map keyed by lcore_id
    threads_[config.lcore_id] = std::move(thread);
  }

  if (verbose) {
    std::cout << "All PMD threads launched successfully\n";
  }

  return absl::OkStatus();
}

void PMDThreadManager::StopAllThreads() {
  stop_flag_.store(true, std::memory_order_relaxed);
}

absl::Status PMDThreadManager::WaitForThreads() {
  // Wait for all worker lcores to finish
  for (const auto& [lcore_id, thread] : threads_) {
    int ret = rte_eal_wait_lcore(lcore_id);
    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("PMD thread on lcore ", lcore_id,
                       " returned error: ", ret));
    }
  }

  return absl::OkStatus();
}

PmdThread* PMDThreadManager::GetThread(uint32_t lcore_id) {
  auto it = threads_.find(lcore_id);
  if (it == threads_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<uint32_t> PMDThreadManager::GetLcoreIds() const {
  std::vector<uint32_t> lcore_ids;
  lcore_ids.reserve(threads_.size());
  for (const auto& [lcore_id, thread] : threads_) {
    lcore_ids.push_back(lcore_id);
  }
  return lcore_ids;
}

}  // namespace dpdk_config
