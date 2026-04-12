#ifndef DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
#define DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "control/command_api.h"
#include "nlohmann/json.hpp"
#include "session/session_table.h"
#include "vm_location/vm_location_table.h"

namespace dpdk_config {

// Forward declaration
class PMDThreadManager;

// CommandHandler processes JSON commands and generates responses.
// It uses the existing JSON parser/printer infrastructure to maintain
// consistency with configuration file processing.
class CommandHandler : public CommandRegistry {
 public:
  // Type alias for async formatted-response delivery.
  using ResponseCallback = std::function<void(const std::string&)>;

  explicit CommandHandler(PMDThreadManager* thread_manager,
                          std::function<void()> shutdown_callback);

  void RegisterSyncCommand(const std::string& name, const std::string& tag,
                           SyncCommandHandler handler) override;
  void RegisterAsyncCommand(const std::string& name, const std::string& tag,
                            AsyncCommandHandler handler) override;

  // Process a JSON command and return JSON response.
  // For synchronous commands, returns the JSON response string.
  // For async commands, returns std::nullopt
  // and the response is sent later via response_cb.
  std::optional<std::string> HandleCommand(
      const std::string& json_command,
      ResponseCallback response_cb = nullptr);

  // Returns the names of all commands whose tag matches the given tag.
  std::vector<std::string> GetCommandsByTag(const std::string& tag) const;

  // Returns all registered commands as {name, tag} pairs.
  std::vector<std::pair<std::string, std::string>> GetAllCommands() const;

  // Set the session table for get_sessions command.
  void SetSessionTable(session::SessionTable* session_table);

  // Set the VmLocation table for get_vm_locations command.
  void SetVmLocationTable(vm_location::VmLocationTable* table);

  // Set FIB metadata for get_fib_info command.
  struct FibInfo {
    uint32_t rules_count = 0;
    uint32_t max_rules = 0;
    uint32_t number_tbl8s = 0;
  };
  void SetFibInfo(const FibInfo& fib_info);

 private:
  struct CommandRequest {
    std::string command;
    nlohmann::json params;
  };

  using CommandResponse = CommandResult;

  struct CommandEntry {
    std::string tag;
    SyncCommandHandler sync_handler;
    AsyncCommandHandler async_handler;
    bool is_async = false;
  };

  absl::StatusOr<CommandRequest> ParseCommand(const std::string& json_str);
  std::string FormatResponse(const CommandResponse& response);

  CommandResponse ExecuteSyncCommand(const CommandRequest& request);
  CommandResponse HandleShutdown(const nlohmann::json& params);
  CommandResponse HandleStatus(const nlohmann::json& params);
  CommandResponse HandleGetThreads(const nlohmann::json& params);
  CommandResponse HandleGetStats(const nlohmann::json& params);
  CommandResponse HandleListCommands(const nlohmann::json& params);
  CommandResponse HandleGetSessions(const nlohmann::json& params);
  CommandResponse HandleGetSessionsCount(const nlohmann::json& params);
  CommandResponse HandleGetFibInfo(const nlohmann::json& params);
  CommandResponse HandleGetVmLocations(const nlohmann::json& params);

  absl::flat_hash_map<std::string, CommandEntry> commands_;
  PMDThreadManager* thread_manager_;  // Not owned
  session::SessionTable* session_table_ = nullptr;  // Not owned
  vm_location::VmLocationTable* vm_location_table_ = nullptr;  // Not owned
  FibInfo fib_info_;
  std::function<void()> shutdown_callback_;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
