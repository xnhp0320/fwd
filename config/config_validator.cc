#include "config/config_validator.h"

#include <algorithm>
#include <iostream>
#include <regex>
#include <unordered_set>

#include "absl/strings/str_cat.h"

namespace dpdk_config {

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
