#ifndef PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_
#define PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "processor/packet_processor_base.h"
#include "processor/packet_stats.h"
#include "processor/processor_context.h"
#include "rxtx/fast_lookup_table.h"

namespace session {
class SessionTable;
}  // namespace session

namespace processor {

class FiveTupleForwardingProcessor
    : public PacketProcessorBase<FiveTupleForwardingProcessor> {
 public:
  explicit FiveTupleForwardingProcessor(
      const dpdk_config::PmdThreadConfig& config,
      PacketStats* stats = nullptr);

  // Check: return InvalidArgument if tx_queues is empty, OK otherwise.
  absl::Status check_impl(
      const std::vector<dpdk_config::QueueAssignment>& rx_queues,
      const std::vector<dpdk_config::QueueAssignment>& tx_queues);

  // Process: burst RX → parse → Find/Insert → tx_burst → free unsent.
  void process_impl();

  // Validate per-processor configuration parameters.
  // Accepts "capacity" (must be a positive integer). Rejects unrecognized keys.
  // Empty map is always OK.
  static absl::Status CheckParams(
      const absl::flat_hash_map<std::string, std::string>& params);

  // Read-only access to the flow table capacity (for testing).
  std::size_t table_capacity() const { return table_.capacity(); }

  // Export the flow table pointer into the processor context.
  // Also reads session_table from context (set by ControlPlane before launch).
  void ExportProcessorData(ProcessorContext& ctx) {
    ctx.processor_data = &table_;
    if (ctx.session_table) {
      session_table_ = static_cast<session::SessionTable*>(ctx.session_table);
    }
  }

 private:
  static constexpr uint16_t kBatchSize = 32;
  static constexpr std::size_t kDefaultCapacity = 65536;
  PacketStats* stats_ = nullptr;
  session::SessionTable* session_table_ = nullptr;
  rxtx::FastLookupTable<> table_;
};

}  // namespace processor

#endif  // PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_
