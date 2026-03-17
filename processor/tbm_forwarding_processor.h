#ifndef PROCESSOR_TBM_FORWARDING_PROCESSOR_H_
#define PROCESSOR_TBM_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "processor/packet_processor_base.h"
#include "processor/packet_stats.h"
#include "processor/processor_context.h"

extern "C" {
#include "tbm/tbmlib.h"
}

namespace processor {

class TbmForwardingProcessor
    : public PacketProcessorBase<TbmForwardingProcessor> {
 public:
  explicit TbmForwardingProcessor(
      const dpdk_config::PmdThreadConfig& config,
      PacketStats* stats = nullptr)
      : PacketProcessorBase(config), stats_(stats) {}

  // Check: return InvalidArgument if tx_queues is empty, OK otherwise.
  absl::Status check_impl(
      const std::vector<dpdk_config::QueueAssignment>& rx_queues,
      const std::vector<dpdk_config::QueueAssignment>& tx_queues);

  // Process: burst RX → parse → TBM lookup → tx_burst → free unsent.
  void process_impl();

  // Export processor-specific data into the context.
  // Caches ctx.tbm_table as FibTbm*. Does NOT touch session_table,
  // processor_data, or lpm_table.
  void ExportProcessorData(ProcessorContext& ctx);

  // Validate per-processor configuration parameters.
  // TbmForwardingProcessor accepts no parameters; any non-empty map is
  // rejected with InvalidArgument listing the unrecognized key.
  static absl::Status CheckParams(
      const absl::flat_hash_map<std::string, std::string>& params);

 private:
  static constexpr uint16_t kBatchSize = 64;
  PacketStats* stats_ = nullptr;
  FibTbm* tbm_table_ = nullptr;  // Borrowed from ProcessorContext
};

}  // namespace processor

#endif  // PROCESSOR_TBM_FORWARDING_PROCESSOR_H_
