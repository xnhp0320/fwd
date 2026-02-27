#include "config/dpdk_initializer.h"

#include <iostream>
#include <rte_eal.h>
#include <rte_errno.h>

#include "absl/strings/str_cat.h"

namespace dpdk_config {

std::vector<std::string> DpdkInitializer::BuildArguments(
    const DpdkConfig& config, const std::string& program_name) {
  std::vector<std::string> args;

  // argv[0] is always the program name
  args.push_back(program_name);

  // Add core mask
  if (config.core_mask.has_value()) {
    args.push_back("-c");
    args.push_back(*config.core_mask);
  }

  // Add memory channels
  if (config.memory_channels.has_value()) {
    args.push_back("-n");
    args.push_back(std::to_string(*config.memory_channels));
  }

  // Add PCI allowlist
  for (const auto& addr : config.pci_allowlist) {
    args.push_back("-a");
    args.push_back(addr);
  }

  // Add PCI blocklist
  for (const auto& addr : config.pci_blocklist) {
    args.push_back("-b");
    args.push_back(addr);
  }

  // Add log level
  if (config.log_level.has_value()) {
    args.push_back("--log-level");
    args.push_back(std::to_string(*config.log_level));
  }

  return args;
}

absl::Status DpdkInitializer::Initialize(const DpdkConfig& config,
                                          const std::string& program_name,
                                          bool verbose) {
  // Build argument vector
  std::vector<std::string> args = BuildArguments(config, program_name);

  if (verbose) {
    std::cout << "DPDK initialization arguments: ";
    for (const auto& arg : args) {
      std::cout << arg << " ";
    }
    std::cout << "\n";
  }

  // Convert to C-style argc/argv
  std::vector<char*> argv;
  for (auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  int argc = argv.size();

  // Call rte_eal_init
  int ret = rte_eal_init(argc, argv.data());

  if (ret < 0) {
    return absl::InternalError(absl::StrCat(
        "DPDK initialization failed: ", rte_strerror(rte_errno)));
  }

  if (verbose) {
    std::cout << "DPDK initialization successful\n";
  }

  return absl::OkStatus();
}

}  // namespace dpdk_config
