#ifndef DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
#define DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_

#include <functional>
#include <string>

#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"

namespace dpdk_config {

// Forward declaration
class PMDThreadManager;

// CommandHandler processes JSON commands and generates responses.
// It uses the existing JSON parser/printer infrastructure to maintain
// consistency with configuration file processing.
class CommandHandler {
 public:
  explicit CommandHandler(PMDThreadManager* thread_manager,
                          std::function<void()> shutdown_callback);

  // Process a JSON command and return JSON response.
  // Errors are encoded in the JSON response, not as StatusOr.
  std::string HandleCommand(const std::string& json_command);

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

  absl::StatusOr<CommandRequest> ParseCommand(const std::string& json_str);
  std::string FormatResponse(const CommandResponse& response);

  CommandResponse ExecuteCommand(const CommandRequest& request);
  CommandResponse HandleShutdown(const nlohmann::json& params);
  CommandResponse HandleStatus(const nlohmann::json& params);
  CommandResponse HandleGetThreads(const nlohmann::json& params);

  PMDThreadManager* thread_manager_;  // Not owned
  std::function<void()> shutdown_callback_;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_COMMAND_HANDLER_H_
