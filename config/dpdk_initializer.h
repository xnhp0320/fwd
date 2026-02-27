#ifndef CONFIG_DPDK_INITIALIZER_H_
#define CONFIG_DPDK_INITIALIZER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

class DpdkInitializer {
 public:
  // Initialize DPDK with the given configuration
  // Returns OK status on success, error status on failure
  static absl::Status Initialize(const DpdkConfig& config,
                                  const std::string& program_name,
                                  bool verbose = false);

  // Construct argv array from configuration (for testing/debugging)
  static std::vector<std::string> BuildArguments(
      const DpdkConfig& config, const std::string& program_name);
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_INITIALIZER_H_
