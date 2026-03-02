#ifndef PROCESSOR_PROCESSOR_CONTEXT_H_
#define PROCESSOR_PROCESSOR_CONTEXT_H_

#include "processor/packet_stats.h"

namespace processor {

// Extensible context passed to processor launchers.
// New per-thread resources go here instead of adding more
// positional args to LauncherFn.
struct ProcessorContext {
  PacketStats* stats = nullptr;
};

}  // namespace processor

#endif  // PROCESSOR_PROCESSOR_CONTEXT_H_
