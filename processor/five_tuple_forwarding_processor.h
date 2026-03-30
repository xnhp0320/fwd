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
#include "rxtx/batch.h"
#include "rxtx/batch_result.h"
// Compile-time toggle: define USE_FAST_LOOKUP_TABLE to use the abseil-backed
// FastLookupTable instead of the default F14-backed F14LookupTable.
#ifndef USE_FAST_LOOKUP_TABLE
#include "rxtx/f14_lookup_table.h"
#else
#include "rxtx/fast_lookup_table.h"
#endif

namespace session {
class SessionTable;
}  // namespace session

namespace processor {

class FiveTupleForwardingProcessor
    : public PacketProcessorBase<FiveTupleForwardingProcessor> {
 public:
  // Per-processor stats for flow-table and session-lookup misses.
  // Single-writer (PMD thread), multi-reader (control plane) via relaxed atomics.
  struct ProcessorStats {
    std::atomic<uint64_t> flow_table_misses{0};
    std::atomic<uint64_t> session_lookup_misses{0};

    void RecordFlowTableMiss() {
      flow_table_misses.store(
          flow_table_misses.load(std::memory_order_relaxed) + 1,
          std::memory_order_relaxed);
    }
    void RecordSessionLookupMiss() {
      session_lookup_misses.store(
          session_lookup_misses.load(std::memory_order_relaxed) + 1,
          std::memory_order_relaxed);
    }
    uint64_t GetFlowTableMisses() const {
      return flow_table_misses.load(std::memory_order_relaxed);
    }
    uint64_t GetSessionLookupMisses() const {
      return session_lookup_misses.load(std::memory_order_relaxed);
    }
  };

  // Compile-time table selection: F14LookupTable by default,
  // define USE_FAST_LOOKUP_TABLE to use the abseil-backed FastLookupTable.
#ifndef USE_FAST_LOOKUP_TABLE
  using FlowTable = rxtx::F14LookupTable<>;
#else
  using FlowTable = rxtx::FastLookupTable<>;
#endif

  // Per-PMD opaque data exported to ProcessorContext for control-plane access.
  struct PmdData {
    FlowTable* table;
    ProcessorStats* stats;
  };

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

  // Export processor-specific data into the context.
  // Also reads session_table from context (set by ControlPlane before launch).
  void ExportProcessorData(ProcessorContext& ctx) {
    pmd_data_ = {&table_, &proc_stats_};
    ctx.processor_data = &pmd_data_;
    if (ctx.session_table) {
      session_table_ = static_cast<session::SessionTable*>(ctx.session_table);
    }
    job_runner_ = ctx.pmd_job_runner;
    if (job_runner_ != nullptr && !gc_job_registered_) {
      gc_job_registered_ = job_runner_->Register(&flow_gc_job_);
    }
  }

 private:
  friend class FiveTupleForwardingProcessorTestAccess;

  static constexpr uint16_t kBatchSize = 32;
  static constexpr std::size_t kGcBatchSize = 16;
  static constexpr std::size_t kDefaultCapacity = 65536;
  using PacketBatch = rxtx::Batch<kBatchSize>;
  using LookupResultBatch = rxtx::BatchResult<rxtx::LookupEntry*, kBatchSize>;

  void ParseBatch(PacketBatch& batch);
  void LookupL1AndSplit(PacketBatch& parsed_batch,
                        LookupResultBatch& hit_results,
                        PacketBatch& miss_batch);
  void BuildMissResultsAndResolveSessions(PacketBatch& miss_batch,
                                          LookupResultBatch& miss_results);
  void ResolveSessions(LookupResultBatch& lookup_results,
                       bool record_session_lookup_miss);
  void RefreshGcScheduling();
  bool ShouldTriggerGc() const;
  void RunFlowGc(uint64_t now_tsc);

  PacketStats* stats_ = nullptr;
  ProcessorStats proc_stats_;
  session::SessionTable* session_table_ = nullptr;
  processor::PmdJobRunner* job_runner_ = nullptr;
  processor::PmdJob flow_gc_job_;
  bool gc_job_registered_ = false;
  bool gc_job_scheduled_ = false;
  uint16_t max_batch_count_ = 0;
  FlowTable table_;
  PmdData pmd_data_{};
};

}  // namespace processor

#endif  // PROCESSOR_FIVE_TUPLE_FORWARDING_PROCESSOR_H_
