#ifndef CONFIG_CONFIG_PARSER_H_
#define CONFIG_CONFIG_PARSER_H_

#include <string>

#include "absl/status/statusor.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

// ConfigParser provides static methods for parsing JSON configuration files
// into DpdkConfig structures. It supports both file-based and string-based
// parsing with comprehensive error reporting.
//
// Example usage:
//   auto config_or = ConfigParser::ParseFile("/path/to/config.json");
//   if (!config_or.ok()) {
//     std::cerr << "Parse error: " << config_or.status() << "\n";
//     return;
//   }
//   const DpdkConfig& config = *config_or;
class ConfigParser {
 public:
  // Parse JSON configuration file at the given path.
  //
  // Returns:
  //   - DpdkConfig on success
  //   - Error status if:
  //     * File does not exist (NotFound)
  //     * File cannot be read (PermissionDenied, etc.)
  //     * JSON syntax is invalid (InvalidArgument with line number)
  //     * File is empty (InvalidArgument)
  //
  // Requirements: 2.1, 2.2
  static absl::StatusOr<DpdkConfig> ParseFile(const std::string& file_path);

  // Parse JSON configuration from a string.
  //
  // Returns:
  //   - DpdkConfig on success
  //   - Error status if:
  //     * JSON syntax is invalid (InvalidArgument with error details)
  //     * Content is empty (InvalidArgument)
  //
  // Requirements: 2.2
  static absl::StatusOr<DpdkConfig> ParseString(const std::string& json_content);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_PARSER_H_
