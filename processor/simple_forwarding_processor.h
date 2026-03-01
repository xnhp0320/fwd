#ifndef PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
#define PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "processor/packet_processor_base.h"

namespace processor {

class SimpleForwardingProcessor
    : public PacketProcessorBase<SimpleForwardingProcessor> {
 public:
  using PacketProcessorBase::PacketProcessorBase;

  // Check: requires exactly 1 TX queue, any number of RX queues.
  absl::Status check_impl(
      const std::vector<dpdk_config::QueueAssignment>& rx_queues,
      const std::vector<dpdk_config::QueueAssignment>& tx_queues);

  // Process: drain all RX queues into the single TX queue.
  void process_impl();

 private:
  static constexpr uint16_t kBatchSize = 32;
};

}  // namespace processor

#endif  // PROCESSOR_SIMPLE_FORWARDING_PROCESSOR_H_
