#ifndef PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
#define PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_

#include <vector>

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

  // Read-only access to this processor's stats counters.
  const PacketStats& GetStats() const { return *stats_; }

 private:
  static constexpr uint16_t kBatchSize = 32;
  PacketStats* stats_ = nullptr;
};

}  // namespace processor

#endif  // PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
