#include "config/pmd_thread.h"

#include <iostream>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"

namespace dpdk_config {

PmdThread::PmdThread(const PmdThreadConfig& config, std::atomic<bool>* stop_flag)
    : config_(config), stop_flag_ptr_(stop_flag) {}

int PmdThread::RunStub(void* arg) {
  if (arg == nullptr) {
    std::cerr << "PMD thread received null argument\n";
    return -1;
  }

  // Cast argument back to PMDThread instance
  PmdThread* thread = static_cast<PmdThread*>(arg);
  return thread->Run();
}

int PmdThread::Run() {
  unsigned lcore_id = rte_lcore_id();
  std::cout << "PMD thread started on lcore " << lcore_id << "\n";
  std::cout << "  RX queues: " << config_.rx_queues.size() << "\n";
  std::cout << "  TX queues: " << config_.tx_queues.size() << "\n";

  // Main packet processing loop
  while (!stop_flag_ptr_->load(std::memory_order_relaxed)) {
    // Process packets for all assigned queues
    // TODO: Implement packet processing logic
    // This function will:
    // 1. Receive packets from RX queues using rte_eth_rx_burst()
    // 2. Process packets (currently left blank)
    // 3. Transmit packets to TX queues using rte_eth_tx_burst()
    
    // Placeholder: yield to avoid busy-waiting in empty implementation
    rte_pause();
  }

  std::cout << "PMD thread on lcore " << lcore_id << " stopping\n";
  return 0;
}

}  // namespace dpdk_config
