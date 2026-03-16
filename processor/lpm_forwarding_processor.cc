#include "processor/lpm_forwarding_processor.h"

#include <arpa/inet.h>

#include <rte_ethdev.h>
#include <rte_lpm.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"
#include "rxtx/batch.h"
#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"

namespace processor {

absl::Status LpmForwardingProcessor::check_impl(
    const std::vector<dpdk_config::QueueAssignment>& /*rx_queues*/,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues) {
  if (tx_queues.empty()) {
    return absl::InvalidArgumentError(
        "lpm_forwarding requires at least one TX queue");
  }
  return absl::OkStatus();
}

void LpmForwardingProcessor::ExportProcessorData(ProcessorContext& ctx) {
  if (ctx.lpm_table) {
    lpm_table_ = static_cast<struct rte_lpm*>(ctx.lpm_table);
  }
}

absl::Status LpmForwardingProcessor::CheckParams(
    const absl::flat_hash_map<std::string, std::string>& params) {
  if (params.empty()) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("unrecognized parameter: ", params.begin()->first));
}

void LpmForwardingProcessor::process_impl() {
  const auto& tx = config().tx_queues[0];

  for (const auto& rx : config().rx_queues) {
    rxtx::Batch<kBatchSize> batch;
    batch.SetCount(rte_eth_rx_burst(rx.port_id, rx.queue_id, batch.Data(),
                                    batch.Capacity()));

    if (batch.Count() == 0) {
      batch.Release();
      continue;
    }

    // Parse each packet and perform LPM lookup for IPv4 packets.
    batch.PrefetchForEach<3>([&](rxtx::Packet& pkt) {
      rxtx::PacketMetadata meta{};
      auto result = rxtx::PacketMetadata::Parse(pkt, meta);
      if (result != rxtx::ParseResult::kOk) {
        // Parse failed — skip lookup but still forward the packet.
        return;
      }

      if (lpm_table_ != nullptr && !meta.IsIpv6()) {
        uint32_t next_hop = 0;
        rte_lpm_lookup(lpm_table_, ntohl(meta.dst_ip.v4), &next_hop);
        // next_hop recorded for future use; packet forwarded regardless.
      }
    });

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

REGISTER_PROCESSOR("lpm_forwarding", LpmForwardingProcessor);

}  // namespace processor
