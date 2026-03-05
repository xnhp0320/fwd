#ifndef PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
#define PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "processor/packet_processor_base.h"
#include "processor/packet_stats.h"

namespace processor {

class SimpleForwardingProcessor
    : public PacketProcessorBase<SimpleForwardingProcessor> {
 public:
  // Construct with optional stats pointer (nullptr disables stats recording).
  explicit SimpleForwardingProcessor(
      const dpdk_config::PmdThreadConfig& config,
      PacketStats* stats = nullptr)
      : PacketProcessorBase(config), stats_(stats) {}

  // Check: Always return OK.
  absl::Status check_impl(
      const std::vector<dpdk_config::QueueAssignment>& rx_queues,
      const std::vector<dpdk_config::QueueAssignment>& tx_queues);

  // Process: drain all RX queues into the single TX queue.
  void process_impl();

  // Validate per-processor configuration parameters.
  // SimpleForwardingProcessor accepts no parameters; any non-empty map is
  // rejected with InvalidArgument listing the unrecognized key.
  static absl::Status CheckParams(
      const absl::flat_hash_map<std::string, std::string>& params);

  // Read-only access to this processor's stats counters.
  const PacketStats& GetStats() const { return *stats_; }

 private:
  static constexpr uint16_t kBatchSize = 32;
  PacketStats* stats_ = nullptr;
};

}  // namespace processor

#endif  // PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
