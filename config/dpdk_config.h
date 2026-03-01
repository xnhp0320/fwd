#ifndef CONFIG_DPDK_CONFIG_H_
#define CONFIG_DPDK_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dpdk_config {

// Queue assignment structure for PMD thread configuration
// Represents a single (port, queue) pair assignment
struct QueueAssignment {
  uint16_t port_id;
  uint16_t queue_id;
};

// PMD thread configuration structure
// Represents the configuration for a single PMD thread
struct PmdThreadConfig {
  // The lcore (CPU core) on which the PMD thread runs
  uint32_t lcore_id;
  
  // List of RX queue assignments for this PMD thread
  std::vector<QueueAssignment> rx_queues;
  
  // List of TX queue assignments for this PMD thread
  std::vector<QueueAssignment> tx_queues;

  // Processor name for this PMD thread (empty string = use default processor)
  std::string processor_name;
};

// Port configuration structure for DPDK port initialization
struct DpdkPortConfig {
  // Port ID (required, must be unique)
  uint16_t port_id;
  
  // Number of RX queues (required, must be > 0)
  uint16_t num_rx_queues;
  
  // Number of TX queues (required, must be > 0)
  uint16_t num_tx_queues;
  
  // Number of descriptors per RX/TX queue (required, must be power of 2)
  uint16_t num_descriptors;
  
  // Mbuf pool size - total number of mbufs in the pool (required, must be > 0)
  uint32_t mbuf_pool_size;
  
  // Mbuf size - data room size for packet buffers (required, must be > 0)
  // Common values: 2048 (standard Ethernet), 9216 (jumbo frames)
  // Should be set to maximum expected packet size
  uint16_t mbuf_size;
};

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

  // Port configurations
  std::vector<DpdkPortConfig> ports;

  // PMD thread configurations
  std::vector<PmdThreadConfig> pmd_threads;

  // Additional EAL parameters as key-value pairs for extensibility
  // Allows passing arbitrary parameters not explicitly defined above
  std::vector<std::pair<std::string, std::string>> additional_params;
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_CONFIG_H_
