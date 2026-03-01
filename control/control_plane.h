#ifndef DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_
#define DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "boost/asio/io_context.hpp"
#include "rcu/rcu_manager.h"

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
  PMDThreadManager* thread_manager_;  // Not owned
  Config config_;
  std::unique_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<rcu::RcuManager> rcu_manager_;
  std::unique_ptr<UnixSocketServer> socket_server_;
  std::unique_ptr<SignalHandler> signal_handler_;
  std::unique_ptr<CommandHandler> command_handler_;
  bool shutdown_initiated_ = false;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_CONTROL_PLANE_H_
