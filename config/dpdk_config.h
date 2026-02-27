#ifndef CONFIG_DPDK_CONFIG_H_
#define CONFIG_DPDK_CONFIG_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dpdk_config {

// Configuration structure for DPDK EAL initialization parameters.
// All fields are optional to support flexible configuration files.
struct DpdkConfig {
  // Core mask for CPU cores to use (hexadecimal string, e.g., "0xff")
  // Maps to DPDK -c argument
  std::optional<std::string> core_mask;

  // Number of memory channels to use (positive integer)
  // Maps to DPDK -n argument
  std::optional<int> memory_channels;

  // PCI devices to allow (whitelist)
  // Each entry should be in format DDDD:BB:DD.F
  // Maps to DPDK -a arguments
  std::vector<std::string> pci_allowlist;

  // PCI devices to block (blacklist)
  // Each entry should be in format DDDD:BB:DD.F
  // Maps to DPDK -b arguments
  std::vector<std::string> pci_blocklist;

  // Log level (0-8, where 8 is most verbose)
  // Maps to DPDK --log-level argument
  std::optional<int> log_level;

  // Number of huge pages to use (positive integer)
  // Reserved for future use or custom handling
  std::optional<int> huge_pages;

  // Additional EAL parameters as key-value pairs for extensibility
  // Allows passing arbitrary parameters not explicitly defined above
  std::vector<std::pair<std::string, std::string>> additional_params;
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_CONFIG_H_
