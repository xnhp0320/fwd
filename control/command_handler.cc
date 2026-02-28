#include "control/command_handler.h"

#include <rte_lcore.h>

#include "absl/strings/str_cat.h"
#include "config/pmd_thread_manager.h"
#include "nlohmann/json.hpp"

namespace dpdk_config {

using json = nlohmann::json;

CommandHandler::CommandHandler(PMDThreadManager* thread_manager,
                               std::function<void()> shutdown_callback)
    : thread_manager_(thread_manager),
      shutdown_callback_(std::move(shutdown_callback)) {}

std::string CommandHandler::HandleCommand(const std::string& json_command) {
  // Parse the command
  auto parse_result = ParseCommand(json_command);
  if (!parse_result.ok()) {
    // Return error response for parsing failures
    CommandResponse error_response;
    error_response.status = "error";
    error_response.error = std::string(parse_result.status().message());
    return FormatResponse(error_response);
  }

  // Execute the command
  CommandResponse response = ExecuteCommand(*parse_result);
  return FormatResponse(response);
}

absl::StatusOr<CommandHandler::CommandRequest> CommandHandler::ParseCommand(
    const std::string& json_str) {
  // Check for empty content
  if (json_str.empty()) {
    return absl::InvalidArgumentError("Command content is empty");
  }

  // Parse JSON
  json j;
  try {
    j = json::parse(json_str);
  } catch (const json::parse_error& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("JSON parse error at byte ", e.byte, ": ", e.what()));
  }

  // Ensure root is an object
  if (!j.is_object()) {
    return absl::InvalidArgumentError("Command must be a JSON object");
  }

  // Extract required "command" field
  if (!j.contains("command")) {
    return absl::InvalidArgumentError("Missing required field: command");
  }

  if (!j["command"].is_string()) {
    return absl::InvalidArgumentError("Field 'command' must be a string");
  }

  CommandRequest request;
  request.command = j["command"].get<std::string>();

  // Extract optional "params" field
  if (j.contains("params")) {
    request.params = j["params"];
  } else {
    request.params = json::object();
  }

  return request;
}

std::string CommandHandler::FormatResponse(const CommandResponse& response) {
  json j;
  j["status"] = response.status;

  if (response.status == "success") {
    j["result"] = response.result;
  } else {
    j["error"] = response.error;
  }

  return j.dump();
}

CommandHandler::CommandResponse CommandHandler::ExecuteCommand(
    const CommandRequest& request) {
  // Dispatch to appropriate handler based on command name
  if (request.command == "shutdown") {
    return HandleShutdown(request.params);
  } else if (request.command == "status") {
    return HandleStatus(request.params);
  } else if (request.command == "get_threads") {
    return HandleGetThreads(request.params);
  } else {
    // Unknown command
    CommandResponse response;
    response.status = "error";
    response.error = absl::StrCat("Unknown command: ", request.command);
    return response;
  }
}

CommandHandler::CommandResponse CommandHandler::HandleShutdown(
    const nlohmann::json& params) {
  CommandResponse response;
  response.status = "success";
  response.result = json::object();
  response.result["message"] = "Shutdown initiated";

  // Invoke the shutdown callback
  if (shutdown_callback_) {
    shutdown_callback_();
  }

  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleStatus(
    const nlohmann::json& params) {
  CommandResponse response;
  response.status = "success";
  response.result = json::object();

  // Get current lcore ID
  response.result["main_lcore"] = rte_lcore_id();

  // Get number of PMD threads
  if (thread_manager_) {
    response.result["num_pmd_threads"] = thread_manager_->GetThreadCount();
  } else {
    response.result["num_pmd_threads"] = 0;
  }

  // Uptime is a future enhancement, set to 0 for now
  response.result["uptime_seconds"] = 0;

  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetThreads(
    const nlohmann::json& params) {
  CommandResponse response;
  response.status = "success";
  response.result = json::object();

  json threads_array = json::array();

  if (thread_manager_) {
    // Get all lcore IDs
    std::vector<uint32_t> lcore_ids = thread_manager_->GetLcoreIds();

    for (uint32_t lcore_id : lcore_ids) {
      json thread_info;
      thread_info["lcore_id"] = lcore_id;
      threads_array.push_back(thread_info);
    }
  }

  response.result["threads"] = threads_array;

  return response;
}

}  // namespace dpdk_config
