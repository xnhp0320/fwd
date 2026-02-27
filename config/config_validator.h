#ifndef CONFIG_CONFIG_VALIDATOR_H_
#define CONFIG_CONFIG_VALIDATOR_H_

#include <string>

#include "absl/status/status.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

// ConfigValidator provides static methods for validating DpdkConfig structures.
// It performs semantic validation of configuration values including format
// checking, range validation, and cross-field consistency checks.
//
// Example usage:
//   DpdkConfig config = ...;
//   absl::Status status = ConfigValidator::Validate(config);
//   if (!status.ok()) {
//     std::cerr << "Validation error: " << status << "\n";
//     return;
//   }
class ConfigValidator {
 public:
  // Validate configuration data.
  //
  // Performs the following validations:
  //   - core_mask: Must be a valid hexadecimal string (0-9, a-f, A-F)
  //   - memory_channels: Must be positive (> 0)
  //   - pci_allowlist: Each address must match format DDDD:BB:DD.F
  //   - pci_blocklist: Each address must match format DDDD:BB:DD.F
  //   - log_level: Must be in range [0, 8]
  //   - huge_pages: Must be positive (> 0)
  //   - No PCI address can appear in both allowlist and blocklist
  //
  // Returns:
  //   - OK status if all validations pass
  //   - InvalidArgument status with descriptive error message if validation fails
  //
  // Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
  static absl::Status Validate(const DpdkConfig& config);

 private:
  // Validate that a string is a valid hexadecimal string.
  // Accepts optional "0x" or "0X" prefix.
  // Valid characters: 0-9, a-f, A-F
  //
  // Requirements: 4.1
  static bool IsValidHexString(const std::string& hex);

  // Validate that a string matches PCI address format: DDDD:BB:DD.F
  // Where:
  //   - DDDD: 4-digit hexadecimal domain
  //   - BB: 2-digit hexadecimal bus
  //   - DD: 2-digit hexadecimal device
  //   - F: 1-digit hexadecimal function
  //
  // Requirements: 4.3
  static bool IsValidPciAddress(const std::string& pci_addr);

  // Validate that a log level is in the valid range [0, 8].
  //
  // Requirements: 4.4
  static bool IsValidLogLevel(int level);

  // Check if a number is a power of 2.
  // Used for validating num_descriptors in port configurations.
  static bool IsPowerOfTwo(uint16_t n);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_VALIDATOR_H_
