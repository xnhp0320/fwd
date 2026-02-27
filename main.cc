#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_join.h"
#include "config/config_parser.h"
#include "config/config_printer.h"
#include "config/config_validator.h"
#include "config/dpdk_initializer.h"
#include "rte_eal.h"

// Define command-line flags
ABSL_FLAG(bool, verbose, false, "Enable verbose output");
ABSL_FLAG(std::string, i, "", "Path to JSON configuration file");

int main(int argc, char **argv) {
  // Parse command-line flags
  absl::ParseCommandLine(argc, argv);

  // Get flag values
  bool verbose = absl::GetFlag(FLAGS_verbose);
  std::string config_file = absl::GetFlag(FLAGS_i);

  if (verbose) {
    std::cout << "Verbose mode enabled\n";
  }

  // Initialize DPDK with configuration file or default arguments
  if (!config_file.empty()) {
    // Load configuration from file
    auto config_or = dpdk_config::ConfigParser::ParseFile(config_file);
    if (!config_or.ok()) {
      std::cerr << "Configuration error: " << config_or.status() << "\n";
      return 1;
    }

    // Validate configuration
    auto validation_status =
        dpdk_config::ConfigValidator::Validate(*config_or);
    if (!validation_status.ok()) {
      std::cerr << "Validation error: " << validation_status << "\n";
      return 1;
    }

    if (verbose) {
      std::cout << "Loaded configuration:\n";
      std::cout << dpdk_config::ConfigPrinter::ToJson(*config_or) << "\n";
    }

    // Initialize DPDK with configuration
    auto init_status = dpdk_config::DpdkInitializer::Initialize(
        *config_or, argv[0], verbose);
    if (!init_status.ok()) {
      std::cerr << "DPDK initialization error: " << init_status << "\n";
      return 1;
    }
  }

  return 0;
}
