#include "config/config_validator.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <unordered_set>

#include "absl/strings/str_cat.h"

namespace dpdk_config {

// Hash function for std::pair to use in unordered_set
template <typename T1, typename T2>
struct PairHash {
  std::size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    // Combine hashes using a simple XOR with bit shift
    return h1 ^ (h2 << 1);
  }
};


bool ConfigValidator::IsValidHexString(const std::string& hex) {
  if (hex.empty()) {
    return false;
  }

  // Check for optional 0x or 0X prefix
  size_t start = 0;
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    start = 2;
  }

  // Must have at least one hex digit after prefix
  if (start >= hex.size()) {
    return false;
  }

  // Check that all remaining characters are valid hex digits
  for (size_t i = start; i < hex.size(); ++i) {
    char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }

  return true;
}

bool ConfigValidator::IsValidPciAddress(const std::string& pci_addr) {
  // PCI address format: DDDD:BB:DD.F
  // DDDD: 4-digit hexadecimal domain
  // BB: 2-digit hexadecimal bus
  // DD: 2-digit hexadecimal device
  // F: 1-digit hexadecimal function
  static const std::regex pci_regex(
      "^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-9a-fA-F]$");
  return std::regex_match(pci_addr, pci_regex);
}

bool ConfigValidator::IsValidLogLevel(int level) {
  return level >= 0 && level <= 8;
}

bool ConfigValidator::IsPowerOfTwo(uint16_t n) {
  // A number is a power of 2 if it has exactly one bit set
  // n & (n-1) clears the lowest set bit, so it's 0 only for powers of 2
  // Also check n > 0 to exclude 0
  return n > 0 && (n & (n - 1)) == 0;
}

std::unordered_set<uint32_t> ConfigValidator::ParseCoremask(
    const std::optional<std::string>& core_mask) {
  std::unordered_set<uint32_t> lcores;
  
  // Return empty set if core_mask is not provided
  if (!core_mask.has_value() || core_mask->empty()) {
    return lcores;
  }
  
  std::string hex_str = *core_mask;
  
  // Remove 0x or 0X prefix if present
  if (hex_str.size() >= 2 && hex_str[0] == '0' && 
      (hex_str[1] == 'x' || hex_str[1] == 'X')) {
    hex_str = hex_str.substr(2);
  }
  
  // Convert hex string to 64-bit integer
  // Use strtoull for 64-bit support
  char* end_ptr;
  uint64_t mask_value = std::strtoull(hex_str.c_str(), &end_ptr, 16);
  
  // Extract bit positions (lcore IDs) from the mask
  for (uint32_t bit_position = 0; bit_position < 64; ++bit_position) {
    if ((mask_value & (1ULL << bit_position)) != 0) {
      lcores.insert(bit_position);
    }
  }
  
  return lcores;
}

uint32_t ConfigValidator::DetermineMainLcore(
    const std::optional<std::string>& core_mask) {
  // Parse the coremask to get available lcores
  std::unordered_set<uint32_t> lcores = ParseCoremask(core_mask);
  
  // If no lcores are available, return 0 as default
  if (lcores.empty()) {
    return 0;
  }
  
  // Return the lowest-numbered lcore (main lcore)
  return *std::min_element(lcores.begin(), lcores.end());
}

const DpdkPortConfig* ConfigValidator::FindPort(
    const std::vector<DpdkPortConfig>& ports, uint16_t port_id) {
  for (const auto& port : ports) {
    if (port.port_id == port_id) {
      return &port;
    }
  }
  return nullptr;
}

absl::Status ConfigValidator::Validate(const DpdkConfig& config) {
  // Validate core_mask format
  if (config.core_mask.has_value()) {
    if (!IsValidHexString(*config.core_mask)) {
      return absl::InvalidArgumentError(
          "core_mask must be a valid hexadecimal string");
    }
  }

  // Validate memory_channels range
  if (config.memory_channels.has_value()) {
    if (*config.memory_channels <= 0) {
      return absl::InvalidArgumentError("memory_channels must be positive");
    }
  }

  // Validate PCI allowlist addresses
  for (const auto& addr : config.pci_allowlist) {
    if (!IsValidPciAddress(addr)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid PCI address in allowlist: ", addr));
    }
  }

  // Validate PCI blocklist addresses
  for (const auto& addr : config.pci_blocklist) {
    if (!IsValidPciAddress(addr)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid PCI address in blocklist: ", addr));
    }
  }

  // Check for PCI address conflicts between allowlist and blocklist
  for (const auto& addr : config.pci_allowlist) {
    if (std::find(config.pci_blocklist.begin(), config.pci_blocklist.end(),
                  addr) != config.pci_blocklist.end()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "PCI address appears in both allowlist and blocklist: ", addr));
    }
  }

  // Validate log_level range
  if (config.log_level.has_value()) {
    if (!IsValidLogLevel(*config.log_level)) {
      return absl::InvalidArgumentError("log_level must be between 0 and 8");
    }
  }

  // Validate huge_pages range
  if (config.huge_pages.has_value()) {
    if (*config.huge_pages <= 0) {
      return absl::InvalidArgumentError("huge_pages must be positive");
    }
  }

  // Validate PMD thread configurations
  if (!config.pmd_threads.empty()) {
    // Get available lcores from coremask
    std::unordered_set<uint32_t> available_lcores = ParseCoremask(config.core_mask);
    
    // Determine main lcore
    uint32_t main_lcore = DetermineMainLcore(config.core_mask);
    
    // Check if there are worker lcores available (lcores other than main lcore)
    std::unordered_set<uint32_t> worker_lcores;
    for (uint32_t lcore : available_lcores) {
      if (lcore != main_lcore) {
        worker_lcores.insert(lcore);
      }
    }
    
    if (worker_lcores.empty()) {
      return absl::InvalidArgumentError(
          "No worker lcores available (coremask only contains main lcore)");
    }
    
    // Validate lcore assignments for each PMD thread
    std::unordered_set<uint32_t> seen_lcores;
    
    for (const auto& pmd_config : config.pmd_threads) {
      uint32_t lcore = pmd_config.lcore_id;
      
      // Check if lcore is the main lcore
      if (lcore == main_lcore) {
        return absl::InvalidArgumentError(
            absl::StrCat("PMD thread cannot use main lcore ", lcore, 
                         " (reserved for control plane)"));
      }
      
      // Check if lcore is in coremask
      if (available_lcores.find(lcore) == available_lcores.end()) {
        return absl::InvalidArgumentError(
            absl::StrCat("PMD thread lcore ", lcore, " is not in coremask"));
      }
      
      // Check for duplicate lcore assignments
      if (seen_lcores.count(lcore) > 0) {
        return absl::InvalidArgumentError(
            absl::StrCat("Duplicate lcore assignment: ", lcore));
      }
      seen_lcores.insert(lcore);
    }
    
    // Validate RX queue assignments
    std::unordered_set<std::pair<uint16_t, uint16_t>, 
                       PairHash<uint16_t, uint16_t>> seen_rx_queues;
    
    for (const auto& pmd_config : config.pmd_threads) {
      uint32_t lcore = pmd_config.lcore_id;
      
      for (const auto& queue : pmd_config.rx_queues) {
        // Check if port exists
        const DpdkPortConfig* port = FindPort(config.ports, queue.port_id);
        if (port == nullptr) {
          return absl::InvalidArgumentError(
              absl::StrCat("PMD thread on lcore ", lcore, 
                           ": unknown port ", queue.port_id));
        }
        
        // Check if queue_id is within range
        if (queue.queue_id >= port->num_rx_queues) {
          return absl::InvalidArgumentError(
              absl::StrCat("PMD thread on lcore ", lcore, 
                           ": RX queue ", queue.queue_id, 
                           " out of range for port ", queue.port_id,
                           " (max: ", port->num_rx_queues - 1, ")"));
        }
        
        // Check for duplicate queue assignments
        std::pair<uint16_t, uint16_t> queue_pair = 
            std::make_pair(queue.port_id, queue.queue_id);
        if (seen_rx_queues.count(queue_pair) > 0) {
          return absl::InvalidArgumentError(
              absl::StrCat("Duplicate RX queue assignment: port ", 
                           queue.port_id, ", queue ", queue.queue_id));
        }
        seen_rx_queues.insert(queue_pair);
      }
    }
    
    // Validate TX queue assignments
    std::unordered_set<std::pair<uint16_t, uint16_t>, 
                       PairHash<uint16_t, uint16_t>> seen_tx_queues;
    
    for (const auto& pmd_config : config.pmd_threads) {
      uint32_t lcore = pmd_config.lcore_id;
      
      for (const auto& queue : pmd_config.tx_queues) {
        // Check if port exists
        const DpdkPortConfig* port = FindPort(config.ports, queue.port_id);
        if (port == nullptr) {
          return absl::InvalidArgumentError(
              absl::StrCat("PMD thread on lcore ", lcore, 
                           ": unknown port ", queue.port_id));
        }
        
        // Check if queue_id is within range
        if (queue.queue_id >= port->num_tx_queues) {
          return absl::InvalidArgumentError(
              absl::StrCat("PMD thread on lcore ", lcore, 
                           ": TX queue ", queue.queue_id, 
                           " out of range for port ", queue.port_id,
                           " (max: ", port->num_tx_queues - 1, ")"));
        }
        
        // Check for duplicate queue assignments
        std::pair<uint16_t, uint16_t> queue_pair = 
            std::make_pair(queue.port_id, queue.queue_id);
        if (seen_tx_queues.count(queue_pair) > 0) {
          return absl::InvalidArgumentError(
              absl::StrCat("Duplicate TX queue assignment: port ", 
                           queue.port_id, ", queue ", queue.queue_id));
        }
        seen_tx_queues.insert(queue_pair);
      }
    }
  }

  // Validate port configurations
  std::unordered_set<uint16_t> seen_port_ids;
  
  for (const auto& port : config.ports) {
    // Check for duplicate port IDs
    if (seen_port_ids.count(port.port_id) > 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate port_id: ", port.port_id));
    }
    seen_port_ids.insert(port.port_id);
    
    // Validate num_rx_queues
    if (port.num_rx_queues == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": num_rx_queues must be > 0"));
    }
    
    // Validate num_tx_queues
    if (port.num_tx_queues == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": num_tx_queues must be > 0"));
    }
    
    // Validate num_descriptors is power of 2
    if (!IsPowerOfTwo(port.num_descriptors)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, 
                       ": num_descriptors must be a power of 2"));
    }
    
    // Validate mbuf_pool_size
    if (port.mbuf_pool_size == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": mbuf_pool_size must be > 0"));
    }
    
    // Recommend minimum pool size accounting for per-core caches
    // Formula: descriptors × queues + cache headroom (512 = ~2 cores × 256 cache)
    uint32_t min_recommended = port.num_descriptors * 
                               (port.num_rx_queues + port.num_tx_queues) + 512;
    if (port.mbuf_pool_size < min_recommended) {
      // This is a warning, not an error - log but don't fail validation
      std::cerr << "Warning: Port " << port.port_id 
                << " mbuf_pool_size (" << port.mbuf_pool_size 
                << ") is below recommended minimum (" << min_recommended
                << "). Consider increasing to account for per-core caches.\n";
    }
    
    // Validate mbuf_size
    if (port.mbuf_size == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": mbuf_size must be > 0"));
    }
  }

  return absl::OkStatus();
}

}  // namespace dpdk_config
