#include "config/pmd_thread.h"

#include <iostream>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"

namespace dpdk_config {

PmdThread::PmdThread(const PmdThreadConfig& config, std::atomic<bool>* stop_flag,
                     struct rte_rcu_qsbr* qsbr_var)
    : config_(config), stop_flag_ptr_(stop_flag), qsbr_var_(qsbr_var) {}

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
  // Look up the launcher from the registry (already validated at startup).
  auto& registry = processor::ProcessorRegistry::Instance();
  std::string name = config_.processor_name.empty()
      ? processor::ProcessorRegistry::kDefaultProcessorName
      : config_.processor_name;

  auto entry_or = registry.Lookup(name);
  if (!entry_or.ok()) {
    std::cerr << "Processor lookup failed: " << entry_or.status() << "\n";
    return -1;
  }

  // Enter the monomorphized hot loop.
  return (*entry_or)->launcher(config_, stop_flag_ptr_, qsbr_var_);
}

}  // namespace dpdk_config
