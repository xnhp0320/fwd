#include "processor/five_tuple_forwarding_processor.h"

#include <cstdlib>

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"
#include "rxtx/batch.h"
#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"
#include "session/session_entry.h"
#include "session/session_key.h"
#include "session/session_table.h"

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

    // Parse each packet and perform two-tier flow-table / session lookup.
    for (uint16_t i = 0; i < batch.Count(); ++i) {
      rxtx::Packet& pkt = rxtx::Packet::from(batch.Data()[i]);
      rxtx::PacketMetadata meta{};
      auto result = rxtx::PacketMetadata::Parse(pkt, meta);
      if (result != rxtx::ParseResult::kOk) {
        // Parse failed — skip lookup but still forward the packet.
        continue;
      }

      rxtx::LookupEntry* entry = table_.Find(meta);

      if (entry != nullptr && session_table_ != nullptr) {
        // L1 hit — validate session version.
        if (entry->session != nullptr) {
          auto* se =
              static_cast<session::SessionEntry*>(entry->session);
          uint32_t current_ver =
              se->version.load(std::memory_order_relaxed);
          if (entry->cached_version == current_ver) {
            // Fast path: session valid, update timestamp.
            se->timestamp.store(rte_rdtsc(), std::memory_order_relaxed);
            continue;
          }
          // Version mismatch: invalidate cached pointer.
          entry->session = nullptr;
          entry->cached_version = 0;
        }

        // L2 lookup (session pointer is null — initial or invalidated).
        session::SessionKey session_key =
            session::SessionKey::FromMetadata(meta, /*zone_id=*/0);
        session::SessionEntry* session = session_table_->Lookup(session_key);
        if (session == nullptr) {
          session = session_table_->Insert(session_key);
        }
        if (session != nullptr) {
          entry->session = session;
          entry->cached_version =
              session->version.load(std::memory_order_relaxed);
          session->timestamp.store(rte_rdtsc(), std::memory_order_relaxed);
        }

      } else if (entry == nullptr) {
        // L1 miss — insert into FastLookupTable.
        entry = table_.Insert(meta.src_ip, meta.dst_ip, meta.src_port,
                              meta.dst_port, meta.protocol, meta.vni,
                              static_cast<uint8_t>(meta.flags & rxtx::kFlagIpv6));
        if (entry != nullptr && session_table_ != nullptr) {
          session::SessionKey session_key =
              session::SessionKey::FromMetadata(meta, /*zone_id=*/0);
          session::SessionEntry* session =
              session_table_->Lookup(session_key);
          if (session == nullptr) {
            session = session_table_->Insert(session_key);
          }
          if (session != nullptr) {
            entry->session = session;
            entry->cached_version =
                session->version.load(std::memory_order_relaxed);
            session->timestamp.store(rte_rdtsc(), std::memory_order_relaxed);
          }
        }
      }
      // If entry != nullptr && session_table_ == nullptr: L1 hit, no session
      // ops — backward compatible behavior.
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
