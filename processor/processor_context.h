#ifndef PROCESSOR_PROCESSOR_CONTEXT_H_
#define PROCESSOR_PROCESSOR_CONTEXT_H_

#include <functional>

#include "processor/packet_stats.h"
#include "rxtx/lookup_entry.h"

namespace processor {

class PmdJobRunner;

// Read-only flow-table inspector exposed to the control plane.
// Implementations are owned by packet processors and remain valid
// for the PMD thread lifetime.
class FlowTableInspector {
 public:
  using EntryVisitor = std::function<void(const rxtx::LookupEntry&)>;

  virtual ~FlowTableInspector() = default;
  virtual void SetModifiable(bool modifiable) = 0;
  virtual void ForEachEntry(const EntryVisitor& visitor) const = 0;
};

// Extensible context passed to processor launchers.
// New per-thread resources go here instead of adding more
// positional args to LauncherFn.
struct ProcessorContext {
  PacketStats* stats = nullptr;
  FlowTableInspector* flow_table_inspector = nullptr;  // nullptr if unsupported
  void* session_table = nullptr;   // SessionTable* (or nullptr if disabled)
  void* proc_stats = nullptr;      // Processor-specific stats (or nullptr)
  PmdJobRunner* pmd_job_runner = nullptr;  // nullptr if not configured
};

}  // namespace processor

#endif  // PROCESSOR_PROCESSOR_CONTEXT_H_
