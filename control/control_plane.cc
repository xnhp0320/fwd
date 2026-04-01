#include "control/control_plane.h"

#include <sys/stat.h>
#include <unistd.h>
#include <rte_lcore.h>
#include <rte_lpm.h>

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <unordered_set>

#include "absl/strings/str_cat.h"
#include "config/pmd_thread_manager.h"
#include "control/command_handler.h"
#include "fib/fib_loader.h"
#include "control/signal_handler.h"
#include "control/unix_socket_server.h"
#include "processor/processor_registry.h"
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

  // Create SessionTable if configured.
  if (config_.session_capacity > 0) {
    if (config_.session_hash_type != "rte_hash") {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported session_hash_type: ",
                       config_.session_hash_type));
    }
    session_table_ = std::make_unique<session::SessionTable>();
    session::SessionTable::Config st_config;
    st_config.capacity = config_.session_capacity;
    auto st_status = session_table_->Init(st_config, rcu_manager_->GetQsbrVar());
    if (!st_status.ok()) return st_status;

    // Wire session table into each PMD thread's ProcessorContext.
    if (thread_manager_) {
      for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
        PmdThread* thread = thread_manager_->GetThread(lcore_id);
        if (thread) {
          thread->GetMutableProcessorContext().session_table =
              session_table_.get();
        }
      }
    }
  }

  // Create LPM table if fib_file is configured with lpm algorithm.
  if (!config_.fib_file.empty() && config_.fib_algorithm == "lpm") {
    struct rte_lpm_config lpm_conf;
    memset(&lpm_conf, 0, sizeof(lpm_conf));
    lpm_conf.max_rules = 1048576;    // 1M rules
    lpm_conf.number_tbl8s = 65536;   // 64K tbl8s for 1M prefixes
    lpm_conf.flags = 0;

    lpm_table_ = rte_lpm_create("fib_lpm", SOCKET_ID_ANY, &lpm_conf);
    if (lpm_table_ == nullptr) {
      return absl::InternalError("rte_lpm_create failed");
    }

    lpm_max_rules_ = lpm_conf.max_rules;
    lpm_number_tbl8s_ = lpm_conf.number_tbl8s;

    auto status = fib::LoadFibFile(config_.fib_file, lpm_table_,
                                   &lpm_rules_loaded_);
    if (!status.ok()) {
      rte_lpm_free(lpm_table_);
      lpm_table_ = nullptr;
      return status;
    }

    // Wire LPM table into each PMD thread's ProcessorContext.
    if (thread_manager_) {
      for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
        PmdThread* thread = thread_manager_->GetThread(lcore_id);
        if (thread) {
          thread->GetMutableProcessorContext().lpm_table = lpm_table_;
        }
      }
    }
  }

  // Create TBM table if fib_file is configured with tbm algorithm.
  if (!config_.fib_file.empty() && config_.fib_algorithm == "tbm") {
    tbm_table_ = {};  // Zero-init before tbm_init
    tbm_init(&tbm_table_, 1048576);  // Match LPM max_rules capacity
    tbm_initialized_ = true;

    auto status = fib::LoadFibFileToTbm(config_.fib_file, &tbm_table_,
                                         &tbm_rules_loaded_);
    if (!status.ok()) {
      tbm_free(&tbm_table_);
      tbm_initialized_ = false;
      return status;
    }

    // Wire TBM table into each PMD thread's ProcessorContext.
    if (thread_manager_) {
      for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
        PmdThread* thread = thread_manager_->GetThread(lcore_id);
        if (thread) {
          thread->GetMutableProcessorContext().tbm_table = &tbm_table_;
        }
      }
    }
  }

  // Initialize CommandHandler with shutdown callback
  command_handler_ = std::make_unique<CommandHandler>(
      thread_manager_,
      [this]() { Shutdown(); });

  // Wire session table into the command handler for get_sessions command.
  if (session_table_) {
    command_handler_->SetSessionTable(session_table_.get());
  }

  // Wire FIB info into the command handler for get_fib_info command.
  {
    CommandHandler::FibInfo fi;
    fi.rules_count = lpm_rules_loaded_;
    fi.max_rules = lpm_max_rules_;
    fi.number_tbl8s = lpm_number_tbl8s_;
    command_handler_->SetFibInfo(fi);
  }

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

  auto register_status = RegisterProcessorCommands();
  if (!register_status.ok()) {
    return register_status;
  }

  // Start signal handler
  signal_handler_->Start();

  // Start Unix socket server with command handler callback
  auto status = socket_server_->Start(
      [this](const std::string& message,
             std::function<void(const std::string&)> response_callback) {
        // Process command and send response.
        // If HandleCommand returns a value, send it immediately.
        // If std::nullopt, the handler will call response_callback later (async).
        auto response = command_handler_->HandleCommand(message, response_callback);
        if (response.has_value()) {
          response_callback(*response);
        }
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

absl::Status ControlPlane::RegisterProcessorCommands() {
  if (processor_commands_registered_ || command_handler_ == nullptr ||
      thread_manager_ == nullptr) {
    return absl::OkStatus();
  }

  dpdk_config::ProcessorCommandRuntime runtime;
  runtime.get_lcore_ids = [this]() {
    return thread_manager_ ? thread_manager_->GetLcoreIds()
                           : std::vector<uint32_t>{};
  };
  runtime.get_processor_data = [this](uint32_t lcore_id) -> void* {
    if (!thread_manager_) return nullptr;
    PmdThread* thread = thread_manager_->GetThread(lcore_id);
    if (thread == nullptr) return nullptr;
    return thread->GetProcessorContext().processor_data;
  };
  runtime.call_after_grace_period = [this](std::function<void()> cb) {
    if (!rcu_manager_) {
      return absl::FailedPreconditionError("RCU manager is not initialized");
    }
    return rcu_manager_->CallAfterGracePeriod(std::move(cb));
  };

  std::unordered_set<std::string> registered_processors;
  auto& registry = processor::ProcessorRegistry::Instance();
  for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
    PmdThread* thread = thread_manager_->GetThread(lcore_id);
    if (thread == nullptr) {
      continue;
    }

    const std::string processor_name =
        thread->GetProcessorName().empty()
            ? processor::ProcessorRegistry::kDefaultProcessorName
            : thread->GetProcessorName();

    if (!registered_processors.insert(processor_name).second) {
      continue;
    }

    auto entry_or = registry.Lookup(processor_name);
    if (!entry_or.ok()) {
      return entry_or.status();
    }

    (*entry_or)->command_registrar(*command_handler_, runtime);
  }

  processor_commands_registered_ = true;
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

  // Destroy LPM table after PMD threads stop.
  if (lpm_table_ != nullptr) {
    rte_lpm_free(lpm_table_);
    lpm_table_ = nullptr;
  }

  // Destroy TBM table after PMD threads stop.
  if (tbm_initialized_) {
    tbm_free(&tbm_table_);
    tbm_initialized_ = false;
  }

  // Destroy SessionTable after PMD threads stop (they hold SessionEntry
  // pointers) but before RCU manager is destroyed (hash references QSBR var).
  session_table_.reset();

  // Stop the event loop
  if (io_context_) {
    io_context_->stop();
  }
}

}  // namespace dpdk_config
