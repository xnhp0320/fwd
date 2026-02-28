#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_join.h"
#include "config/config_parser.h"
#include "config/config_printer.h"
#include "config/config_validator.h"
#include "config/dpdk_initializer.h"
#include "config/pmd_thread_manager.h"
#include "control/control_plane.h"
#include <rte_eal.h>
#include <rte_lcore.h>

// Define command-line flags
ABSL_FLAG(bool, verbose, false, "Enable verbose output");
ABSL_FLAG(std::string, i, "", "Path to JSON configuration file");
ABSL_FLAG(std::string, socket_path, "/tmp/dpdk_control.sock",
          "Path to Unix domain socket for control plane");

int main(int argc, char **argv) {
  // Parse command-line flags
  absl::ParseCommandLine(argc, argv);

  // Get flag values
  bool verbose = absl::GetFlag(FLAGS_verbose);
  std::string config_file = absl::GetFlag(FLAGS_i);
  std::string socket_path = absl::GetFlag(FLAGS_socket_path);

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

    // Initialize DPDK with configuration (includes port init and PMD thread launch)
    auto init_result = dpdk_config::DpdkInitializer::Initialize(
        *config_or, argv[0], verbose);
    if (!init_result.ok()) {
      std::cerr << "DPDK initialization error: " << init_result.status() << "\n";
      return 1;
    }

    // Get the thread manager
    auto thread_manager = std::move(*init_result);

    std::cout << "DPDK initialized successfully\n";
    std::cout << "Main thread running on lcore " << rte_lcore_id() 
              << " (control plane)\n";

    // Create and initialize the control plane
    dpdk_config::ControlPlane control_plane(thread_manager.get());
    
    dpdk_config::ControlPlane::Config control_config;
    control_config.socket_path = socket_path;
    
    auto init_status = control_plane.Initialize(control_config);
    if (!init_status.ok()) {
      std::cerr << "Control plane initialization error: " << init_status << "\n";
      return 1;
    }

    std::cout << "Control plane initialized on socket: " << socket_path << "\n";
    std::cout << "Control plane ready\n";
    std::cout << "Press Ctrl+C to exit...\n";
    std::cout.flush();

    // Run the event loop (blocks until shutdown)
    auto run_status = control_plane.Run();
    if (!run_status.ok()) {
      std::cerr << "Control plane runtime error: " << run_status << "\n";
      return 1;
    }

    std::cout << "Control plane shutdown complete\n";
  }

  return 0;
}
