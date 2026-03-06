#include "control/command_handler.h"

#include <arpa/inet.h>
#include <rte_lcore.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "config/pmd_thread.h"
#include "config/pmd_thread_manager.h"
#include "nlohmann/json.hpp"
#include "rcu/rcu_manager.h"
#include "rxtx/fast_lookup_table.h"
#include "rxtx/lookup_entry.h"
#include "session/session_key.h"

namespace dpdk_config {

using json = nlohmann::json;

CommandHandler::CommandHandler(PMDThreadManager* thread_manager,
                               std::function<void()> shutdown_callback)
    : thread_manager_(thread_manager),
      shutdown_callback_(std::move(shutdown_callback)) {
  RegisterCommand("shutdown", "common",
                   [this](const json& params) { return HandleShutdown(params); });
  RegisterCommand("status", "common",
                   [this](const json& params) { return HandleStatus(params); });
  RegisterCommand("get_threads", "common",
                   [this](const json& params) { return HandleGetThreads(params); });
  RegisterCommand("get_stats", "common",
                   [this](const json& params) { return HandleGetStats(params); });
  RegisterCommand("list_commands", "common",
                   [this](const json& params) { return HandleListCommands(params); });
  RegisterCommand("get_flow_table", "five_tuple_forwarding",
                   [this](const json& params) { return HandleGetFlowTable(params); });
}

void CommandHandler::RegisterCommand(
    const std::string& name, const std::string& tag,
    std::function<CommandResponse(const nlohmann::json&)> handler) {
  commands_[name] = CommandEntry{tag, std::move(handler)};
}

std::optional<std::string> CommandHandler::HandleCommand(
    const std::string& json_command, ResponseCallback response_cb) {
  // Parse the command
  auto parse_result = ParseCommand(json_command);
  if (!parse_result.ok()) {
    // Return error response for parsing failures
    CommandResponse error_response;
    error_response.status = "error";
    error_response.error = std::string(parse_result.status().message());
    return FormatResponse(error_response);
  }

  // Special async handling for get_flow_table when RCU + thread manager available
  if (parse_result->command == "get_flow_table" &&
      rcu_manager_ != nullptr && thread_manager_ != nullptr &&
      response_cb != nullptr) {
    HandleGetFlowTableAsync(std::move(response_cb));
    return std::nullopt;  // Response will be sent asynchronously
  }

  // Execute the command synchronously
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
  auto it = commands_.find(request.command);
  if (it != commands_.end()) {
    return it->second.handler(request.params);
  }

  // Unknown command
  CommandResponse response;
  response.status = "error";
  response.error = absl::StrCat("Unknown command: ", request.command);
  return response;
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

void CommandHandler::SetRcuManager(rcu::RcuManager* rcu_manager) {
  rcu_manager_ = rcu_manager;
}

void CommandHandler::SetSessionTable(session::SessionTable* session_table) {
  session_table_ = session_table;
  if (session_table_ != nullptr) {
    RegisterCommand("get_sessions", "session",
                     [this](const json& params) { return HandleGetSessions(params); });
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

CommandHandler::CommandResponse CommandHandler::HandleGetStats(
    const nlohmann::json& params) {
  CommandResponse response;
  response.status = "success";

  json threads_array = json::array();
  uint64_t total_packets = 0;
  uint64_t total_bytes = 0;

  if (thread_manager_) {
    for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
      PmdThread* thread = thread_manager_->GetThread(lcore_id);
      if (thread && thread->GetStats()) {
        uint64_t pkts = thread->GetStats()->GetPackets();
        uint64_t byts = thread->GetStats()->GetBytes();
        threads_array.push_back({{"lcore_id", lcore_id},
                                 {"packets", pkts},
                                 {"bytes", byts}});
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
  CommandResponse response;
  response.status = "success";

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

CommandHandler::CommandResponse CommandHandler::HandleGetFlowTable(
    const nlohmann::json& params) {
  // Synchronous fallback: returned when rcu_manager_ or thread_manager_ is
  // unavailable (the async path in HandleCommand bypasses this entirely).
  CommandResponse response;
  response.status = "error";
  response.error = "not_supported";
  return response;
}

CommandHandler::CommandResponse CommandHandler::HandleGetSessions(
    const nlohmann::json& params) {
  CommandResponse response;
  response.status = "success";

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
      return false;  // never delete
    });
  }

  response.result = {{"sessions", sessions_array}};
  return response;
}

void CommandHandler::HandleGetFlowTableAsync(ResponseCallback response_cb) {
  // Phase 1: Collect tables from all threads and pause modifications.
  struct TableInfo {
    uint32_t lcore_id;
    rxtx::FastLookupTable<>* table;  // nullptr for non-FiveTuple threads
  };
  auto tables = std::make_shared<std::vector<TableInfo>>();

  for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
    PmdThread* thread = thread_manager_->GetThread(lcore_id);
    const auto& ctx = thread->GetProcessorContext();
    auto* table =
        static_cast<rxtx::FastLookupTable<>*>(ctx.processor_data);
    tables->push_back({lcore_id, table});
    if (table) {
      table->SetModifiable(false);
    }
  }

  // Phase 2: Schedule read after grace period completes.
  // Share response_cb so it can be used both in the lambda and the error path.
  auto shared_cb = std::make_shared<ResponseCallback>(std::move(response_cb));
  auto status = rcu_manager_->CallAfterGracePeriod(
      [tables, shared_cb, this]() {
        json threads_array = json::array();

        for (const auto& info : *tables) {
          if (info.table == nullptr) {
            threads_array.push_back(
                {{"lcore_id", info.lcore_id}, {"entries", json::array()}});
            continue;
          }

          json entries = json::array();
          try {
            for (auto it = info.table->Begin(); it != info.table->End();
                 ++it) {
              rxtx::LookupEntry* entry = *it;
              json e;
              char ip_buf[INET6_ADDRSTRLEN];

              if (entry->IsIpv6()) {
                inet_ntop(AF_INET6, entry->src_ip.v6, ip_buf, sizeof(ip_buf));
                e["src_ip"] = ip_buf;
                inet_ntop(AF_INET6, entry->dst_ip.v6, ip_buf, sizeof(ip_buf));
                e["dst_ip"] = ip_buf;
              } else {
                inet_ntop(AF_INET, &entry->src_ip.v4, ip_buf, sizeof(ip_buf));
                e["src_ip"] = ip_buf;
                inet_ntop(AF_INET, &entry->dst_ip.v4, ip_buf, sizeof(ip_buf));
                e["dst_ip"] = ip_buf;
              }

              e["src_port"] = entry->src_port;
              e["dst_port"] = entry->dst_port;
              e["protocol"] = entry->protocol;
              e["vni"] = entry->vni;
              e["is_ipv6"] = entry->IsIpv6();
              entries.push_back(std::move(e));
            }
          } catch (...) {
            info.table->SetModifiable(true);
            CommandResponse err;
            err.status = "error";
            err.error = "Failed to serialize flow table entries";
            (*shared_cb)(FormatResponse(err));
            return;
          }
          info.table->SetModifiable(true);
          threads_array.push_back(
              {{"lcore_id", info.lcore_id}, {"entries", entries}});
        }

        CommandResponse resp;
        resp.status = "success";
        resp.result = {{"threads", threads_array}};
        (*shared_cb)(FormatResponse(resp));
      });

  if (!status.ok()) {
    // Grace period scheduling failed — restore all tables to modifiable.
    for (const auto& info : *tables) {
      if (info.table) {
        info.table->SetModifiable(true);
      }
    }
    CommandResponse err;
    err.status = "error";
    err.error = "Failed to schedule grace period";
    (*shared_cb)(FormatResponse(err));
  }
}

}  // namespace dpdk_config
