#ifndef CONFIG_DPDK_INITIALIZER_H_
#define CONFIG_DPDK_INITIALIZER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/dpdk_config.h"
#include "config/pmd_thread_manager.h"

namespace dpdk_config {

class DpdkInitializer {
 public:
  // Initialize DPDK with the given configuration
  // Returns a PMDThreadManager on success, error status on failure
  // The returned manager owns the launched PMD threads
  static absl::StatusOr<std::unique_ptr<PMDThreadManager>> Initialize(
      const DpdkConfig& config, const std::string& program_name,
      bool verbose = false);

  // Construct argv array from configuration (for testing/debugging)
  static std::vector<std::string> BuildArguments(
      const DpdkConfig& config, const std::string& program_name);
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_INITIALIZER_H_
