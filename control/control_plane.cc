#include "control/control_plane.h"

#include <sys/stat.h>
#include <unistd.h>
#include <rte_lcore.h>

#include <chrono>
#include <future>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "config/pmd_thread_manager.h"
#include "control/command_handler.h"
#include "control/signal_handler.h"
#include "control/unix_socket_server.h"
#include "rcu/rcu_manager.h"

namespace dpdk_config {

ControlPlane::ControlPlane(PMDThreadManager* thread_manager)
    : thread_manager_(thread_manager) {}

ControlPlane::~ControlPlane() {
  // Ensure cleanup happens
  if (io_context_) {
    Shutdown();
  }
}

absl::Status ControlPlane::Initialize(const Config& config) {
  config_ = config;

  // Verify execution on main lcore
  unsigned current_lcore = rte_lcore_id();
  unsigned main_lcore = rte_get_main_lcore();
  
  if (current_lcore != main_lcore) {
    return absl::FailedPreconditionError(
        absl::StrCat("ControlPlane must be initialized on main lcore (", 
                     main_lcore, "), but running on lcore ", current_lcore));
  }

  // Validate socket path directory exists and is writable
  // Extract directory from socket path
  std::string socket_dir = config_.socket_path;
  size_t last_slash = socket_dir.find_last_of('/');
  if (last_slash != std::string::npos) {
    socket_dir = socket_dir.substr(0, last_slash);
  } else {
    socket_dir = ".";
  }

  // Check if directory exists
  struct stat st;
  if (stat(socket_dir.c_str(), &st) != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Socket path directory does not exist: ", socket_dir));
  }

  // Check if it's a directory
  if (!S_ISDIR(st.st_mode)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Socket path parent is not a directory: ", socket_dir));
  }

  // Check if directory is writable
  if (access(socket_dir.c_str(), W_OK) != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Socket path directory is not writable: ", socket_dir));
  }

  // Create io_context
  io_context_ = std::make_unique<boost::asio::io_context>();

  // Create and initialize the RCU manager
  rcu_manager_ = std::make_unique<rcu::RcuManager>();
  rcu::RcuManager::Config rcu_config;
  auto rcu_status = rcu_manager_->Init(*io_context_, rcu_config);
  if (!rcu_status.ok()) {
    return rcu_status;
  }

  // Wire RCU manager into the thread manager for automatic
  // thread registration and quiescent state reporting.
  if (thread_manager_) {
    thread_manager_->SetRcuManager(rcu_manager_.get());
  }

  // Initialize CommandHandler with shutdown callback
  command_handler_ = std::make_unique<CommandHandler>(
      thread_manager_,
      [this]() { Shutdown(); });

  // Initialize UnixSocketServer
  socket_server_ = std::make_unique<UnixSocketServer>(
      *io_context_,
      config_.socket_path);

  // Initialize SignalHandler with shutdown callback
  signal_handler_ = std::make_unique<SignalHandler>(
      *io_context_,
      [this]() { Shutdown(); });

  std::cout << "ControlPlane initialized on lcore " << main_lcore << "\n";
  
  return absl::OkStatus();
}

absl::Status ControlPlane::Run() {
  if (!io_context_) {
    return absl::FailedPreconditionError(
        "ControlPlane not initialized. Call Initialize() first.");
  }

  // Start signal handler
  signal_handler_->Start();

  // Start Unix socket server with command handler callback
  auto status = socket_server_->Start(
      [this](const std::string& message,
             std::function<void(const std::string&)> response_callback) {
        // Process command and send response
        std::string response = command_handler_->HandleCommand(message);
        response_callback(response);
      });

  if (!status.ok()) {
    return status;
  }

  std::cout << "ControlPlane running, event loop started\n";
  std::cout.flush();

  // Start the RCU poll timer before entering the event loop
  if (rcu_manager_) {
    auto rcu_status = rcu_manager_->Start();
    if (!rcu_status.ok()) {
      return rcu_status;
    }
  }

  // Run the event loop (blocks until shutdown)
  io_context_->run();

  std::cout << "ControlPlane event loop stopped\n";
  
  return absl::OkStatus();
}

void ControlPlane::Shutdown() {
  if (shutdown_initiated_) {
    return;  // Already shutting down
  }

  shutdown_initiated_ = true;
  std::cout << "ControlPlane shutdown initiated\n";

  // Stop accepting new connections
  if (socket_server_) {
    socket_server_->Stop();
  }

  // Stop signal handler
  if (signal_handler_) {
    signal_handler_->Stop();
  }

  // Stop RCU manager before stopping PMD threads.
  // This cancels the poll timer and discards pending callbacks.
  if (rcu_manager_) {
    rcu_manager_->Stop();
  }

  // Stop PMD threads with timeout
  if (thread_manager_) {
    std::cout << "Stopping PMD threads...\n";
    thread_manager_->StopAllThreads();
    
    // Wait for PMD threads to complete with timeout
    auto timeout_duration = std::chrono::seconds(config_.shutdown_timeout_seconds);
    auto start_time = std::chrono::steady_clock::now();
    
    // Launch WaitForThreads in a separate thread so we can timeout
    std::future<absl::Status> wait_future = std::async(std::launch::async, [this]() {
      return thread_manager_->WaitForThreads();
    });
    
    // Wait with timeout
    auto wait_status = wait_future.wait_for(timeout_duration);
    
    if (wait_status == std::future_status::timeout) {
      auto elapsed = std::chrono::steady_clock::now() - start_time;
      auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      std::cerr << "WARNING: Shutdown timeout exceeded (" << elapsed_seconds 
                << " seconds). PMD threads did not stop within " 
                << config_.shutdown_timeout_seconds << " seconds.\n";
    } else {
      auto status = wait_future.get();
      if (!status.ok()) {
        std::cerr << "Error waiting for PMD threads: " 
                  << status.message() << "\n";
      } else {
        std::cout << "All PMD threads stopped\n";
      }
    }
  }

  // Stop the event loop
  if (io_context_) {
    io_context_->stop();
  }
}

}  // namespace dpdk_config
