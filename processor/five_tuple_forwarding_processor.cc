#include "processor/five_tuple_forwarding_processor.h"

#include <cstdlib>

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"
#include "rxtx/batch.h"
#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"

namespace processor {

FiveTupleForwardingProcessor::FiveTupleForwardingProcessor(
    const dpdk_config::PmdThreadConfig& config, PacketStats* stats)
    : PacketProcessorBase(config),
      stats_(stats),
      table_([&config]() -> std::size_t {
        auto it = config.processor_params.find("capacity");
        if (it != config.processor_params.end()) {
          return static_cast<std::size_t>(std::atol(it->second.c_str()));
        }
        return kDefaultCapacity;
      }()) {}

absl::Status FiveTupleForwardingProcessor::check_impl(
    const std::vector<dpdk_config::QueueAssignment>& /*rx_queues*/,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues) {
  if (tx_queues.empty()) {
    return absl::InvalidArgumentError(
        "five_tuple_forwarding requires at least one TX queue");
  }
  return absl::OkStatus();
}

void FiveTupleForwardingProcessor::process_impl() {
  const auto& tx = config().tx_queues[0];

  for (const auto& rx : config().rx_queues) {
    rxtx::Batch<kBatchSize> batch;
    batch.SetCount(rte_eth_rx_burst(rx.port_id, rx.queue_id, batch.Data(),
                                    batch.Capacity()));

    if (batch.Count() == 0) {
      batch.Release();
      continue;
    }

    // Parse each packet and perform flow-table lookup/insert.
    for (uint16_t i = 0; i < batch.Count(); ++i) {
      rxtx::Packet& pkt = rxtx::Packet::from(batch.Data()[i]);
      rxtx::PacketMetadata meta{};
      auto result = rxtx::PacketMetadata::Parse(pkt, meta);
      if (result == rxtx::ParseResult::kOk) {
        rxtx::LookupEntry* entry = table_.Find(meta);
        if (entry == nullptr) {
          table_.Insert(meta.src_ip, meta.dst_ip, meta.src_port, meta.dst_port,
                        meta.protocol, meta.vni,
                        static_cast<uint8_t>(meta.flags & rxtx::kFlagIpv6));
        }
      }
      // If parse fails, skip lookup but still forward the packet.
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

absl::Status FiveTupleForwardingProcessor::CheckParams(
    const absl::flat_hash_map<std::string, std::string>& params) {
  for (const auto& [key, value] : params) {
    if (key == "capacity") {
      // Must be a positive integer.
      if (value.empty()) {
        return absl::InvalidArgumentError(
            "capacity must be a positive integer");
      }
      char* end = nullptr;
      long val = std::strtol(value.c_str(), &end, 10);
      if (end == value.c_str() || *end != '\0' || val <= 0) {
        return absl::InvalidArgumentError(
            absl::StrCat("capacity must be a positive integer, got: ", value));
      }
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unrecognized parameter: ", key));
    }
  }
  return absl::OkStatus();
}

REGISTER_PROCESSOR("five_tuple_forwarding", FiveTupleForwardingProcessor);

}  // namespace processor
