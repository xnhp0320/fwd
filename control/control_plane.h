#ifndef DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_
#define DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "boost/asio/io_context.hpp"
#include "rcu/rcu_manager.h"
#include "fib/fib_loader.h"
#include "session/session_table.h"

namespace dpdk_config {

// Forward declarations
class PMDThreadManager;
class UnixSocketServer;
class SignalHandler;
class CommandHandler;

// ControlPlane orchestrates the event loop for the control plane.
// It runs on the main lcore and provides a Unix domain socket interface
// for receiving JSON-formatted commands while PMD worker threads handle
// packet processing on other lcores.
class ControlPlane {
 public:
  struct Config {
    std::string socket_path = "/tmp/dpdk_control.sock";
    uint32_t shutdown_timeout_seconds = 10;
    uint32_t session_capacity = 0;
    std::string fib_file;
  };

  explicit ControlPlane(PMDThreadManager* thread_manager);
  ~ControlPlane();

  // Initialize the control plane on the main lcore.
  // Returns error if not on main lcore or initialization fails.
  absl::Status Initialize(const Config& config);

  // Run the event loop (blocks until shutdown).
  // Returns after graceful shutdown completes.
  absl::Status Run();

  // Initiate graceful shutdown.
  void Shutdown();

 private:
  absl::Status RegisterProcessorCommands();

  PMDThreadManager* thread_manager_;  // Not owned
  Config config_;
  std::unique_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<rcu::RcuManager> rcu_manager_;
  std::unique_ptr<UnixSocketServer> socket_server_;
  std::unique_ptr<SignalHandler> signal_handler_;
  std::unique_ptr<CommandHandler> command_handler_;
  std::unique_ptr<session::SessionTable> session_table_;
  struct rte_lpm* lpm_table_ = nullptr;
  uint32_t lpm_max_rules_ = 0;
  uint32_t lpm_number_tbl8s_ = 0;
  uint32_t lpm_rules_loaded_ = 0;
  bool processor_commands_registered_ = false;
  bool shutdown_initiated_ = false;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_
