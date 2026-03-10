#ifndef PROCESSOR_PROCESSOR_REGISTRY_H_
#define PROCESSOR_PROCESSOR_REGISTRY_H_

#include <atomic>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_rcu_qsbr.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/dpdk_config.h"
#include "control/command_api.h"
#include "processor/pmd_job.h"
#include "processor/processor_context.h"

namespace processor {

// A launcher function runs the entire monomorphized hot loop for a
// specific processor type. It is called once per PMD thread.
// Returns 0 on success, non-zero on error.
using LauncherFn = std::function<int(
    const dpdk_config::PmdThreadConfig& config,
    std::atomic<bool>* stop_flag,
    struct rte_rcu_qsbr* qsbr_var,
    processor::ProcessorContext& ctx)>;

// A check function validates queue assignments for a processor type.
// Called at startup before entering the hot loop.
using CheckFn = std::function<absl::Status(
    const std::vector<dpdk_config::QueueAssignment>& rx_queues,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues)>;

// A param-check function validates per-processor configuration parameters.
// Called at startup to reject unrecognized or invalid parameter keys/values.
using ParamCheckFn = std::function<absl::Status(
    const absl::flat_hash_map<std::string, std::string>& params)>;

struct ProcessorEntry {
  LauncherFn launcher;
  CheckFn checker;
  ParamCheckFn param_checker;
  dpdk_config::ProcessorCommandRegistrar command_registrar;
};

template <typename ProcessorType, typename = void>
struct HasRegisterControlCommands : std::false_type {};

template <typename ProcessorType>
struct HasRegisterControlCommands<
    ProcessorType,
    std::void_t<decltype(ProcessorType::RegisterControlCommands(
        std::declval<dpdk_config::CommandRegistry&>(),
        std::declval<const dpdk_config::ProcessorCommandRuntime&>()))>>
    : std::true_type {};

class ProcessorRegistry {
 public:
  // Get the singleton instance.
  static ProcessorRegistry& Instance();

  // Register a processor type under a string name.
  // Typically called from a static initializer in each processor's .cc file.
  void Register(const std::string& name, ProcessorEntry entry);

  // Look up a processor by name. Returns NotFoundError if unknown.
  absl::StatusOr<const ProcessorEntry*> Lookup(const std::string& name) const;

  // List all registered processor names (sorted, for error messages).
  std::vector<std::string> RegisteredNames() const;

  // The default processor name used when config omits the field.
  static constexpr const char* kDefaultProcessorName = "simple_forwarding";

 private:
  ProcessorRegistry() = default;
  absl::flat_hash_map<std::string, ProcessorEntry> entries_;
};

// Helper: generates a monomorphized launcher and checker for a CRTP processor
// type.
template <typename ProcessorType>
ProcessorEntry MakeProcessorEntry() {
  return ProcessorEntry{
      // Launcher: constructs the processor and runs the tight loop.
      .launcher =
          [](const dpdk_config::PmdThreadConfig& config,
             std::atomic<bool>* stop_flag,
             struct rte_rcu_qsbr* qsbr_var,
             processor::ProcessorContext& ctx) -> int {
        ProcessorType proc(config, ctx.stats);
        proc.ExportProcessorData(ctx);
        while (!stop_flag->load(std::memory_order_relaxed)) {
          proc.process_impl();  // Direct call — compiler knows exact type.
          if (ctx.pmd_job_runner != nullptr &&
              ctx.pmd_job_runner->HasRunnableJobs()) {
            ctx.pmd_job_runner->RunRunnableJobs(rte_rdtsc());
          }
          if (qsbr_var) {
            rte_rcu_qsbr_quiescent(qsbr_var, rte_lcore_id());
          }
        }
        return 0;
      },
      // Checker: constructs a temporary processor and calls Check().
      .checker =
          [](const std::vector<dpdk_config::QueueAssignment>& rx,
             const std::vector<dpdk_config::QueueAssignment>& tx)
          -> absl::Status {
        dpdk_config::PmdThreadConfig dummy;
        dummy.rx_queues = rx;
        dummy.tx_queues = tx;
        ProcessorType proc(dummy);
        return proc.Check();
      },
      // ParamChecker: delegates to the processor's static CheckParams.
      .param_checker =
          [](const absl::flat_hash_map<std::string, std::string>& params)
          -> absl::Status {
        return ProcessorType::CheckParams(params);
      },
      .command_registrar =
          [](dpdk_config::CommandRegistry& registry,
             const dpdk_config::ProcessorCommandRuntime& runtime) {
            if constexpr (HasRegisterControlCommands<ProcessorType>::value) {
              ProcessorType::RegisterControlCommands(registry, runtime);
            }
          },
  };
}

// Macro for self-registration in a .cc file.
// Usage: REGISTER_PROCESSOR("simple_forwarding", SimpleForwardingProcessor);
#define REGISTER_PROCESSOR(name, type)                              \
  __attribute__((used)) static bool registered_##type = [] {        \
    ::processor::ProcessorRegistry::Instance().Register(            \
        name, ::processor::MakeProcessorEntry<type>());             \
    return true;                                                    \
  }()

}  // namespace processor

#endif  // PROCESSOR_PROCESSOR_REGISTRY_H_
