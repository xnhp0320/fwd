#ifndef PROCESSOR_PROCESSOR_CONTEXT_H_
#define PROCESSOR_PROCESSOR_CONTEXT_H_

#include "processor/packet_stats.h"

namespace processor {

class PmdJobRunner;

// Extensible context passed to processor launchers.
// New per-thread resources go here instead of adding more
// positional args to LauncherFn.
struct ProcessorContext {
  PacketStats* stats = nullptr;
  void* session_table = nullptr;      // SessionTable* (or nullptr if disabled)
  void* processor_data = nullptr;     // Processor-specific opaque data (or nullptr)
  void* lpm_table = nullptr;          // rte_lpm* (or nullptr if no FIB)
  void* tbm_table = nullptr;          // FibTbm* (or nullptr if no TBM FIB)
  PmdJobRunner* pmd_job_runner = nullptr;  // nullptr if not configured
};

}  // namespace processor

#endif  // PROCESSOR_PROCESSOR_CONTEXT_H_
