#ifndef PROCESSOR_PROCESSOR_REGISTRY_H_
#define PROCESSOR_PROCESSOR_REGISTRY_H_

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/dpdk_config.h"

namespace processor {

// A launcher function runs the entire monomorphized hot loop for a
// specific processor type. It is called once per PMD thread.
// Returns 0 on success, non-zero on error.
using LauncherFn = std::function<int(const dpdk_config::PmdThreadConfig& config,
                                     std::atomic<bool>* stop_flag)>;

// A check function validates queue assignments for a processor type.
// Called at startup before entering the hot loop.
using CheckFn = std::function<absl::Status(
    const std::vector<dpdk_config::QueueAssignment>& rx_queues,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues)>;

struct ProcessorEntry {
  LauncherFn launcher;
  CheckFn checker;
};

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
             std::atomic<bool>* stop_flag) -> int {
        ProcessorType proc(config);
        while (!stop_flag->load(std::memory_order_relaxed)) {
          proc.process_impl();  // Direct call — compiler knows exact type.
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
  };
}

// Macro for self-registration in a .cc file.
// Usage: REGISTER_PROCESSOR("simple_forwarding", SimpleForwardingProcessor);
#define REGISTER_PROCESSOR(name, type)                    \
  static bool registered_##type = [] {                    \
    ::processor::ProcessorRegistry::Instance().Register(  \
        name, ::processor::MakeProcessorEntry<type>());   \
    return true;                                          \
  }()

}  // namespace processor

#endif  // PROCESSOR_PROCESSOR_REGISTRY_H_
