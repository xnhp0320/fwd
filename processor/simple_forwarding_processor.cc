#include "processor/simple_forwarding_processor.h"

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"
#include "rxtx/batch.h"

namespace processor {

absl::Status SimpleForwardingProcessor::check_impl(
    const std::vector<dpdk_config::QueueAssignment>& /*rx_queues*/,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues) {
  if (tx_queues.size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "SimpleForwardingProcessor requires exactly 1 TX queue, but ",
        tx_queues.size(), " were assigned"));
  }
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
