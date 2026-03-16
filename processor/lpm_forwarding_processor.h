#ifndef PROCESSOR_LPM_FORWARDING_PROCESSOR_H_
#define PROCESSOR_LPM_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "processor/packet_processor_base.h"
#include "processor/packet_stats.h"
#include "processor/processor_context.h"

struct rte_lpm;

namespace processor {

class LpmForwardingProcessor
    : public PacketProcessorBase<LpmForwardingProcessor> {
 public:
  explicit LpmForwardingProcessor(
      const dpdk_config::PmdThreadConfig& config,
      PacketStats* stats = nullptr)
      : PacketProcessorBase(config), stats_(stats) {}

  // Check: return InvalidArgument if tx_queues is empty, OK otherwise.
  absl::Status check_impl(
      const std::vector<dpdk_config::QueueAssignment>& rx_queues,
      const std::vector<dpdk_config::QueueAssignment>& tx_queues);

  // Process: burst RX → parse → LPM lookup → tx_burst → free unsent.
  void process_impl();

  // Export processor-specific data into the context.
  // Caches ctx.lpm_table as rte_lpm*. Does NOT touch session_table or
  // processor_data.
  void ExportProcessorData(ProcessorContext& ctx);

  // Validate per-processor configuration parameters.
  // LpmForwardingProcessor accepts no parameters; any non-empty map is
  // rejected with InvalidArgument listing the unrecognized key.
  static absl::Status CheckParams(
      const absl::flat_hash_map<std::string, std::string>& params);

 private:
  static constexpr uint16_t kBatchSize = 64;
  PacketStats* stats_ = nullptr;
  struct rte_lpm* lpm_table_ = nullptr;  // Borrowed from ProcessorContext
};

}  // namespace processor

#endif  // PROCESSOR_LPM_FORWARDING_PROCESSOR_H_
