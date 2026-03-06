#ifndef DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
#define DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"
#include "session/session_table.h"

namespace rcu {
class RcuManager;
}  // namespace rcu

namespace dpdk_config {

// Forward declaration
class PMDThreadManager;

// CommandHandler processes JSON commands and generates responses.
// It uses the existing JSON parser/printer infrastructure to maintain
// consistency with configuration file processing.
class CommandHandler {
 public:
  // Type alias for async response delivery.
  using ResponseCallback = std::function<void(const std::string&)>;

  explicit CommandHandler(PMDThreadManager* thread_manager,
                          std::function<void()> shutdown_callback);

  // Process a JSON command and return JSON response.
  // For synchronous commands, returns the JSON response string.
  // For async commands (e.g., get_flow_table), returns std::nullopt
  // and the response is sent later via response_cb.
  std::optional<std::string> HandleCommand(
      const std::string& json_command,
      ResponseCallback response_cb = nullptr);

  // Returns the names of all commands whose tag matches the given tag.
  std::vector<std::string> GetCommandsByTag(const std::string& tag) const;

  // Returns all registered commands as {name, tag} pairs.
  std::vector<std::pair<std::string, std::string>> GetAllCommands() const;

  // Set the RCU manager for async grace-period operations.
  void SetRcuManager(rcu::RcuManager* rcu_manager);

  // Set the session table for get_sessions command.
  void SetSessionTable(session::SessionTable* session_table);

 private:
  struct CommandRequest {
    std::string command;
    nlohmann::json params;
  };

  struct CommandResponse {
    std::string status;  // "success" or "error"
    nlohmann::json result;
    std::string error;
  };

  struct CommandEntry {
    std::string tag;
    std::function<CommandResponse(const nlohmann::json&)> handler;
  };

  absl::StatusOr<CommandRequest> ParseCommand(const std::string& json_str);
  std::string FormatResponse(const CommandResponse& response);

  void RegisterCommand(
      const std::string& name, const std::string& tag,
      std::function<CommandResponse(const nlohmann::json&)> handler);

  CommandResponse ExecuteCommand(const CommandRequest& request);
  CommandResponse HandleShutdown(const nlohmann::json& params);
  CommandResponse HandleStatus(const nlohmann::json& params);
  CommandResponse HandleGetThreads(const nlohmann::json& params);
  CommandResponse HandleGetStats(const nlohmann::json& params);
  CommandResponse HandleListCommands(const nlohmann::json& params);
  CommandResponse HandleGetFlowTable(const nlohmann::json& params);
  CommandResponse HandleGetSessions(const nlohmann::json& params);

  // Two-phase async implementation of get_flow_table.
  // Phase 1: collect tables, SetModifiable(false), schedule grace period.
  // Phase 2 (callback): iterate entries, serialize, SetModifiable(true), respond.
  void HandleGetFlowTableAsync(ResponseCallback response_cb);

  absl::flat_hash_map<std::string, CommandEntry> commands_;
  PMDThreadManager* thread_manager_;  // Not owned
  rcu::RcuManager* rcu_manager_ = nullptr;  // Not owned
  session::SessionTable* session_table_ = nullptr;  // Not owned
  std::function<void()> shutdown_callback_;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
