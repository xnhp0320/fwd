#include "processor/simple_forwarding_processor.h"

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "processor/processor_registry.h"
#include "rxtx/batch.h"

namespace processor {

absl::Status SimpleForwardingProcessor::check_impl(
    const std::vector<dpdk_config::QueueAssignment>& /*rx_queues*/,
    const std::vector<dpdk_config::QueueAssignment>& /*tx_queues*/) {
  return absl::OkStatus();
}

void SimpleForwardingProcessor::process_impl() {
  const auto& tx = config().tx_queues[0];

  for (const auto& rx : config().rx_queues) {
    rxtx::Batch<kBatchSize> batch;
    batch.SetCount(rte_eth_rx_burst(rx.port_id, rx.queue_id, batch.Data(),
                                    batch.Capacity()));

    if (batch.Count() == 0) {
      batch.Release();
      continue;
    }

    // Record per-thread stats before transmitting.
    if (stats_) {
      uint64_t total_bytes = 0;
      for (uint16_t i = 0; i < batch.Count(); ++i) {
        total_bytes += rte_pktmbuf_pkt_len(batch.Data()[i]);
      }
      stats_->RecordBatch(batch.Count(), total_bytes);
    }

    uint16_t sent =
        rte_eth_tx_burst(tx.port_id, tx.queue_id, batch.Data(), batch.Count());

    // Free untransmitted mbufs.
    for (uint16_t i = sent; i < batch.Count(); ++i) {
      rte_pktmbuf_free(batch.Data()[i]);
    }

    // Release ownership so the Batch destructor doesn't double-free.
    batch.Release();
  }
}

REGISTER_PROCESSOR("simple_forwarding", SimpleForwardingProcessor);

}  // namespace processor
