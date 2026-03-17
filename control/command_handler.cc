#include "control/command_handler.h"

#include <arpa/inet.h>
#include <rte_lcore.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "config/pmd_thread.h"
#include "config/pmd_thread_manager.h"
#include "session/session_key.h"

namespace dpdk_config {

using json = nlohmann::json;

CommandHandler::CommandHandler(PMDThreadManager* thread_manager,
                               std::function<void()> shutdown_callback)
    : thread_manager_(thread_manager),
      shutdown_callback_(std::move(shutdown_callback)) {
  RegisterSyncCommand("shutdown", "common",
                      [this](const json& params) { return HandleShutdown(params); });
  RegisterSyncCommand("status", "common",
                      [this](const json& params) { return HandleStatus(params); });
  RegisterSyncCommand("get_threads", "common",
                      [this](const json& params) { return HandleGetThreads(params); });
  RegisterSyncCommand("get_stats", "common",
                      [this](const json& params) { return HandleGetStats(params); });
  RegisterSyncCommand("list_commands", "common",
                      [this](const json& params) { return HandleListCommands(params); });
}

void CommandHandler::RegisterSyncCommand(const std::string& name,
                                         const std::string& tag,
                                         SyncCommandHandler handler) {
  CommandEntry entry;
  entry.tag = tag;
  entry.sync_handler = std::move(handler);
  entry.async_handler = nullptr;
  entry.is_async = false;
  commands_[name] = std::move(entry);
}

void CommandHandler::RegisterAsyncCommand(const std::string& name,
                                          const std::string& tag,
                                          AsyncCommandHandler handler) {
  CommandEntry entry;
  entry.tag = tag;
  entry.sync_handler = nullptr;
  entry.async_handler = std::move(handler);
  entry.is_async = true;
  commands_[name] = std::move(entry);
}

std::optional<std::string> CommandHandler::HandleCommand(
    const std::string& json_command, ResponseCallback response_cb) {
  auto parse_result = ParseCommand(json_command);
  if (!parse_result.ok()) {
    return FormatResponse(CommandResponse::Error(
        std::string(parse_result.status().message())));
  }

  const CommandRequest& request = *parse_result;
  auto it = commands_.find(request.command);
  if (it == commands_.end()) {
    return FormatResponse(
        CommandResponse::Error(absl::StrCat("Unknown command: ", request.command)));
  }

  const CommandEntry& entry = it->second;
  if (entry.is_async) {
    if (response_cb == nullptr || entry.async_handler == nullptr) {
      return FormatResponse(CommandResponse::Error(
          absl::StrCat("Command requires async callback: ", request.command)));
    }
    entry.async_handler(
        request.params, [this, response_cb = std::move(response_cb)](
                            CommandResult response) mutable {
          response_cb(FormatResponse(response));
        });
    return std::nullopt;
  }

  return FormatResponse(ExecuteSyncCommand(request));
}

absl::StatusOr<CommandHandler::CommandRequest> CommandHandler::ParseCommand(
    const std::string& json_str) {
  if (json_str.empty()) {
    return absl::InvalidArgumentError("Command content is empty");
  }

  json j;
  try {
    j = json::parse(json_str);
  } catch (const json::parse_error& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("JSON parse error at byte ", e.byte, ": ", e.what()));
  }

  if (!j.is_object()) {
    return absl::InvalidArgumentError("Command must be a JSON object");
  }

  if (!j.contains("command")) {
    return absl::InvalidArgumentError("Missing required field: command");
  }

  if (!j["command"].is_string()) {
    return absl::InvalidArgumentError("Field 'command' must be a string");
  }

  CommandRequest request;
  request.command = j["command"].get<std::string>();
  request.params = j.contains("params") ? j["params"] : json::object();
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

CommandHandler::CommandResponse CommandHandler::ExecuteSyncCommand(
    const CommandRequest& request) {
  auto it = commands_.find(request.command);
  if (it == commands_.end() || it->second.is_async ||
      it->second.sync_handler == nullptr) {
    return CommandResponse::Error(
        absl::StrCat("Unknown command: ", request.command));
  }
  return it->second.sync_handler(request.params);
}

std::vector<std::string> CommandHandler::GetCommandsByTag(
    const std::string& tag) const {
  std::vector<std::string> result;
  for (const auto& [name, entry] : commands_) {
    if (entry.tag == tag) {
      result.push_back(name);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::pair<std::string, std::string>>
CommandHandler::GetAllCommands() const {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& [name, entry] : commands_) {
    result.emplace_back(name, entry.tag);
  }
  std::sort(result.begin(), result.end());
  return result;
}

void CommandHandler::SetSessionTable(session::SessionTable* session_table) {
  session_table_ = session_table;
  if (session_table_ != nullptr) {
    RegisterSyncCommand("get_sessions", "session",
                        [this](const json& params) { return HandleGetSessions(params); });
    RegisterSyncCommand("get_sessions_count", "session",
                        [this](const json& params) { return HandleGetSessionsCount(params); });
  }
}

void CommandHandler::SetFibInfo(const FibInfo& fib_info) {
  fib_info_ = fib_info;
  RegisterSyncCommand("get_fib_info", "fib",
                      [this](const json& params) { return HandleGetFibInfo(params); });
}

CommandHandler::CommandResponse CommandHandler::HandleShutdown(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());
  response.result["message"] = "Shutdown initiated";

  if (shutdown_callback_) {
    shutdown_callback_();
  }
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleStatus(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());
  response.result["main_lcore"] = rte_lcore_id();
  response.result["num_pmd_threads"] =
      thread_manager_ ? thread_manager_->GetThreadCount() : 0;
  response.result["uptime_seconds"] = 0;
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetThreads(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());
  json threads_array = json::array();

  if (thread_manager_) {
    for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
      threads_array.push_back({{"lcore_id", lcore_id}});
    }
  }

  response.result["threads"] = threads_array;
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetStats(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());

  json threads_array = json::array();
  uint64_t total_packets = 0;
  uint64_t total_bytes = 0;

  if (thread_manager_) {
    for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
      PmdThread* thread = thread_manager_->GetThread(lcore_id);
      if (thread && thread->GetStats()) {
        uint64_t pkts = thread->GetStats()->GetPackets();
        uint64_t byts = thread->GetStats()->GetBytes();
        threads_array.push_back(
            {{"lcore_id", lcore_id}, {"packets", pkts}, {"bytes", byts}});
        total_packets += pkts;
        total_bytes += byts;
      }
    }
  }

  response.result = {{"threads", threads_array},
                     {"total", {{"packets", total_packets},
                                {"bytes", total_bytes}}}};
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleListCommands(
    const nlohmann::json& params) {
  CommandResponse response = CommandResponse::Success(json::object());
  json commands_array = json::array();

  if (params.contains("tag") && params["tag"].is_string()) {
    std::string tag = params["tag"].get<std::string>();
    auto names = GetCommandsByTag(tag);
    for (const auto& name : names) {
      commands_array.push_back({{"name", name}, {"tag", tag}});
    }
  } else {
    auto all = GetAllCommands();
    for (const auto& [name, tag] : all) {
      commands_array.push_back({{"name", name}, {"tag", tag}});
    }
  }

  response.result = {{"commands", commands_array}};
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetSessions(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());
  json sessions_array = json::array();

  if (session_table_ != nullptr) {
    session_table_->ForEach([&sessions_array](const session::SessionKey& key,
                                              session::SessionEntry* entry) {
      json s;
      char ip_buf[INET6_ADDRSTRLEN];
      bool is_ipv6 = (key.flags & 1) != 0;

      if (is_ipv6) {
        inet_ntop(AF_INET6, key.src_ip.v6, ip_buf, sizeof(ip_buf));
        s["src_ip"] = ip_buf;
        inet_ntop(AF_INET6, key.dst_ip.v6, ip_buf, sizeof(ip_buf));
        s["dst_ip"] = ip_buf;
      } else {
        inet_ntop(AF_INET, &key.src_ip.v4, ip_buf, sizeof(ip_buf));
        s["src_ip"] = ip_buf;
        inet_ntop(AF_INET, &key.dst_ip.v4, ip_buf, sizeof(ip_buf));
        s["dst_ip"] = ip_buf;
      }

      s["src_port"] = key.src_port;
      s["dst_port"] = key.dst_port;
      s["protocol"] = key.protocol;
      s["zone_id"] = key.zone_id;
      s["is_ipv6"] = is_ipv6;
      s["version"] = entry->version.load(std::memory_order_relaxed);
      s["timestamp"] = entry->timestamp.load(std::memory_order_relaxed);

      sessions_array.push_back(std::move(s));
      return false;
    });
  }

  response.result = {{"sessions", sessions_array}};
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetSessionsCount(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());
  
  size_t count = 0;
  if (session_table_ != nullptr) {
      count = session_table_->Count();
  }

  response.result = {{"count", count}};
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetFibInfo(
    const nlohmann::json& /*params*/) {
  CommandResponse response = CommandResponse::Success(json::object());

  // tbl24 is always 2^24 entries × 4 bytes = 64 MiB.
  // tbl8 groups: number_tbl8s × 256 entries × 4 bytes each.
  // rule_info: max_rules × 8 bytes (struct rte_lpm_rule stores ip + next_hop).
  uint64_t tbl24_bytes = (1u << 24) * 4u;
  uint64_t tbl8_bytes =
      static_cast<uint64_t>(fib_info_.number_tbl8s) * 256u * 4u;
  uint64_t rules_bytes = static_cast<uint64_t>(fib_info_.max_rules) * 8u;
  uint64_t total_bytes = tbl24_bytes + tbl8_bytes + rules_bytes;

  response.result = {
      {"rules_count", fib_info_.rules_count},
      {"max_rules", fib_info_.max_rules},
      {"number_tbl8s", fib_info_.number_tbl8s},
      {"memory_bytes", total_bytes},
  };
  return response;
}

}  // namespace dpdk_config
