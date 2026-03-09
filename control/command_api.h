#ifndef DPDK_CONFIG_CONTROL_COMMAND_API_H_
#define DPDK_CONFIG_CONTROL_COMMAND_API_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "nlohmann/json.hpp"

namespace processor {
class FlowTableInspector;
}  // namespace processor

namespace dpdk_config {

using json = nlohmann::json;

struct CommandResult {
  std::string status;
  json result;
  std::string error;

  static CommandResult Success(json result = json::object()) {
    return CommandResult{"success", std::move(result), ""};
  }

  static CommandResult Error(std::string error) {
    return CommandResult{"error", json::object(), std::move(error)};
  }
};

using CommandResultCallback = std::function<void(CommandResult)>;
using SyncCommandHandler = std::function<CommandResult(const json&)>;
using AsyncCommandHandler =
    std::function<void(const json&, CommandResultCallback)>;

// Generic command registry interface used by packet processors
// to register processor-specific control commands.
class CommandRegistry {
 public:
  virtual ~CommandRegistry() = default;

  virtual void RegisterSyncCommand(const std::string& name,
                                   const std::string& tag,
                                   SyncCommandHandler handler) = 0;
  virtual void RegisterAsyncCommand(const std::string& name,
                                    const std::string& tag,
                                    AsyncCommandHandler handler) = 0;
};

// Runtime callbacks exposed to processor command registrars.
struct ProcessorCommandRuntime {
  std::function<std::vector<uint32_t>()> get_lcore_ids;
  std::function<processor::FlowTableInspector*(uint32_t)> get_flow_table_inspector;
  std::function<absl::Status(std::function<void()>)> call_after_grace_period;
};

using ProcessorCommandRegistrar =
    std::function<void(CommandRegistry&, const ProcessorCommandRuntime&)>;

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_COMMAND_API_H_
