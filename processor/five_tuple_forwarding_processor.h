#ifndef PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_
#define PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "control/command_api.h"
#include "processor/packet_processor_base.h"
#include "processor/packet_stats.h"
#include "processor/processor_context.h"
#include "processor/pmd_job.h"
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
  ~FiveTupleForwardingProcessor();

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

  static void RegisterControlCommands(
      dpdk_config::CommandRegistry& registry,
      const dpdk_config::ProcessorCommandRuntime& runtime);

  // Read-only access to the flow table capacity (for testing).
  std::size_t table_capacity() const { return table_.capacity(); }

  // Export the flow table pointer into the processor context.
  // Also reads session_table from context (set by ControlPlane before launch).
  void ExportProcessorData(ProcessorContext& ctx) {
    ctx.flow_table_inspector = &flow_table_inspector_;
    if (ctx.session_table) {
      session_table_ = static_cast<session::SessionTable*>(ctx.session_table);
    }
    job_runner_ = ctx.pmd_job_runner;
    if (job_runner_ != nullptr && !gc_job_registered_) {
      gc_job_registered_ = job_runner_->Register(&flow_gc_job_);
    }
  }

 private:
  using FlowTable = rxtx::FastLookupTable<>;

  class FlowTableInspectorImpl final : public processor::FlowTableInspector {
   public:
    explicit FlowTableInspectorImpl(FlowTable* table) : table_(table) {}

    void SetModifiable(bool modifiable) override {
      table_->SetModifiable(modifiable);
    }

    void ForEachEntry(const EntryVisitor& visitor) const override {
      for (auto it = table_->Begin(); it != table_->End(); ++it) {
        visitor(**it);
      }
    }

   private:
    FlowTable* table_;  // Not owned
  };

  static constexpr uint16_t kBatchSize = 32;
  static constexpr std::size_t kGcBatchSize = 16;
  static constexpr std::size_t kDefaultCapacity = 65536;
  void RefreshGcScheduling();
  bool ShouldTriggerGc() const;
  void RunFlowGc(uint64_t now_tsc);

  PacketStats* stats_ = nullptr;
  session::SessionTable* session_table_ = nullptr;
  processor::PmdJobRunner* job_runner_ = nullptr;
  processor::PmdJob flow_gc_job_;
  bool gc_job_registered_ = false;
  bool gc_job_scheduled_ = false;
  uint16_t max_batch_count_ = 0;
  FlowTable table_;
  FlowTableInspectorImpl flow_table_inspector_;
};

}  // namespace processor

#endif  // PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_
