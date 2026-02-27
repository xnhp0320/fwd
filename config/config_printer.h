#ifndef CONFIG_CONFIG_PRINTER_H_
#define CONFIG_CONFIG_PRINTER_H_

#include <string>

#include "config/dpdk_config.h"

namespace dpdk_config {

// ConfigPrinter serializes DpdkConfig structures to JSON format.
// Provides round-trip capability: configurations can be printed to JSON
// and then parsed back to equivalent DpdkConfig structures.
class ConfigPrinter {
 public:
  // Format configuration as JSON string with proper indentation.
  // 
  // Args:
  //   config: The DpdkConfig structure to serialize
  //   indent: Number of spaces for indentation (default: 2)
  //
  // Returns:
  //   JSON string representation of the configuration with proper formatting
  //
  // Requirements: 6.1, 6.2, 6.3
  static std::string ToJson(const DpdkConfig& config, int indent = 2);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_PRINTER_H_
