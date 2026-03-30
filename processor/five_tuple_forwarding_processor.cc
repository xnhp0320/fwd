#include "processor/five_tuple_forwarding_processor.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"
#include "processor/processor_registry.h"
#include "rxtx/batch.h"
#include "rxtx/batch_result.h"
#include "rxtx/packet.h"
#include "rxtx/packet_metadata.h"
#include "session/session_entry.h"
#include "session/session_key.h"
#include "session/session_table.h"

namespace processor {

namespace {

nlohmann::json SerializeLookupEntry(const rxtx::LookupEntry& entry) {
  nlohmann::json e;
  char ip_buf[INET6_ADDRSTRLEN];

  if (entry.IsIpv6()) {
    inet_ntop(AF_INET6, entry.src_ip.v6, ip_buf, sizeof(ip_buf));
    e["src_ip"] = ip_buf;
    inet_ntop(AF_INET6, entry.dst_ip.v6, ip_buf, sizeof(ip_buf));
    e["dst_ip"] = ip_buf;
  } else {
    inet_ntop(AF_INET, &entry.src_ip.v4, ip_buf, sizeof(ip_buf));
    e["src_ip"] = ip_buf;
    inet_ntop(AF_INET, &entry.dst_ip.v4, ip_buf, sizeof(ip_buf));
    e["dst_ip"] = ip_buf;
  }

  e["src_port"] = entry.src_port;
  e["dst_port"] = entry.dst_port;
  e["protocol"] = entry.protocol;
  e["vni"] = entry.vni;
  e["is_ipv6"] = entry.IsIpv6();
  return e;
}

}  // namespace

FiveTupleForwardingProcessor::FiveTupleForwardingProcessor(
    const dpdk_config::PmdThreadConfig& config, PacketStats* stats)
    : PacketProcessorBase(config),
      stats_(stats),
      flow_gc_job_([this](uint64_t now_tsc) { RunFlowGc(now_tsc); }),
      table_([&config]() -> std::size_t {
        auto it = config.processor_params.find("capacity");
        if (it != config.processor_params.end()) {
          return static_cast<std::size_t>(std::atol(it->second.c_str()));
        }
        return kDefaultCapacity;
      }()) {}

FiveTupleForwardingProcessor::~FiveTupleForwardingProcessor() {
  if (job_runner_ != nullptr && gc_job_registered_) {
    (void)job_runner_->Unregister(&flow_gc_job_);
    gc_job_registered_ = false;
    gc_job_scheduled_ = false;
  }
}

absl::Status FiveTupleForwardingProcessor::check_impl(
    const std::vector<dpdk_config::QueueAssignment>& /*rx_queues*/,
    const std::vector<dpdk_config::QueueAssignment>& tx_queues) {
  if (tx_queues.empty()) {
    return absl::InvalidArgumentError(
        "five_tuple_forwarding requires at least one TX queue");
  }
  return absl::OkStatus();
}

void FiveTupleForwardingProcessor::ParseBatch(PacketBatch& batch) {
  batch.PrefetchFilter<1>([&](rxtx::Packet& pkt) -> bool {
    rxtx::PacketMetadata meta{};
    if (rxtx::PacketMetadata::Parse(pkt, meta) != rxtx::ParseResult::kOk) {
      pkt.Free();
      return false;
    }
    pkt.Metadata() = meta;
    return true;
  });
}

void FiveTupleForwardingProcessor::BuildPrefetchContexts(PrefetchContextBatch& contexts) {
  contexts.Build([&](rxtx::Packet& pkt, FlowTable::PrefetchContext& ctx) {
    table_.Prefetch(pkt.Metadata(), ctx);
  });
}

void FiveTupleForwardingProcessor::LookupL1AndSplit(
    PrefetchContextBatch& contexts,
    LookupResultBatch& hit_results, PacketBatch& miss_batch) {
  hit_results.PrefetchFilter<1>(
      [&](rxtx::Packet* pkt, rxtx::LookupEntry*& entry,
          uint16_t idx) -> bool {
        entry = table_.FindWithPrefetch(pkt->Metadata(), contexts.ResultAt(idx));
        if (entry == nullptr) {
          proc_stats_.RecordFlowTableMiss();
          return false;
        }
        if (entry->session != nullptr) {
          rte_prefetch0(entry->session);
        }
        return true;
      },
      miss_batch);
}

void FiveTupleForwardingProcessor::BuildMissResultsAndResolveSessions(
    PacketBatch& miss_batch, LookupResultBatch& miss_results) {
  if (miss_batch.Count() == 0) {
    return;
  }

  miss_results.Build([&](rxtx::Packet& pkt, rxtx::LookupEntry*& entry) {
    const rxtx::PacketMetadata& meta = pkt.Metadata();
    entry = table_.Insert(meta.src_ip, meta.dst_ip, meta.src_port, meta.dst_port,
                          meta.protocol, meta.vni,
                          static_cast<uint8_t>(meta.flags & rxtx::kFlagIpv6));
  });

  ResolveSessions(miss_results, /*record_session_lookup_miss=*/false);
}

void FiveTupleForwardingProcessor::ResolveSessions(
    LookupResultBatch& lookup_results, bool record_session_lookup_miss) {
  if (session_table_ == nullptr) {
    return;
  }

  uint64_t ts = rte_rdtsc();
  lookup_results.ForEach([&](rxtx::Packet& pkt, rxtx::LookupEntry*& entry) {
    if (entry == nullptr) {
      return;
    }

    if (entry->session != nullptr) {
      auto* se = static_cast<session::SessionEntry*>(entry->session);
      uint32_t current_ver = se->version.load(std::memory_order_relaxed);
      if (entry->cached_version == current_ver) {
        se->timestamp.store(ts, std::memory_order_relaxed);
        return;
      }
      entry->session = nullptr;
      entry->cached_version = 0;
    }

    session::SessionKey session_key =
        session::SessionKey::FromMetadata(pkt.Metadata(), /*zone_id=*/0);
    session::SessionEntry* session = session_table_->Lookup(session_key);
    if (session == nullptr) {
      if (record_session_lookup_miss) {
        proc_stats_.RecordSessionLookupMiss();
      }
      session = session_table_->Insert(session_key);
    }
    if (session != nullptr) {
      entry->session = session;
      entry->cached_version = session->version.load(std::memory_order_relaxed);
      session->timestamp.store(ts, std::memory_order_relaxed);
    }
  });
}

void FiveTupleForwardingProcessor::process_impl() {
  max_batch_count_ = 0;

  const auto& tx = config().tx_queues[0];

  for (const auto& rx : config().rx_queues) {
    PacketBatch parsed_batch;
    parsed_batch.SetCount(rte_eth_rx_burst(rx.port_id, rx.queue_id,
                                           parsed_batch.Data(),
                                           parsed_batch.Capacity()));

    // Note: some NIC PMD drivers with limited burst sizes (e.g., virtio, tap)
    // may return fewer packets per burst even under load, which can cause false
    // light-traffic classification.
    max_batch_count_ = std::max(max_batch_count_, parsed_batch.Count());

    if (parsed_batch.Count() == 0) {
      parsed_batch.Release();
      continue;
    }

    ParseBatch(parsed_batch);
    if (parsed_batch.Count() == 0) {
      parsed_batch.Release();
      continue;
    }

    PacketBatch flow_miss_batch;
    PrefetchContextBatch prefetch_contexts(&parsed_batch);
    LookupResultBatch l1_hit_results(&parsed_batch);
    LookupResultBatch flow_miss_results(&flow_miss_batch);
    BuildPrefetchContexts(prefetch_contexts);
    LookupL1AndSplit(prefetch_contexts, l1_hit_results,
                     flow_miss_batch);
    ResolveSessions(l1_hit_results, /*record_session_lookup_miss=*/true);
    BuildMissResultsAndResolveSessions(flow_miss_batch, flow_miss_results);

    // Record per-thread stats before transmitting.
    if (stats_) {
      uint64_t total_bytes = 0;
      for (uint16_t i = 0; i < parsed_batch.Count(); ++i) {
        total_bytes += rte_pktmbuf_pkt_len(parsed_batch.Data()[i]);
      }
      for (uint16_t i = 0; i < flow_miss_batch.Count(); ++i) {
        total_bytes += rte_pktmbuf_pkt_len(flow_miss_batch.Data()[i]);
      }
      stats_->RecordBatch(parsed_batch.Count() + flow_miss_batch.Count(),
                          total_bytes);
    }

    const uint16_t parsed_count = parsed_batch.Count();
    const uint16_t miss_count = flow_miss_batch.Count();
    const uint16_t merged_count = parsed_count + miss_count;
    for (uint16_t i = 0; i < miss_count; ++i) {
      parsed_batch.Data()[parsed_count + i] = flow_miss_batch.Data()[i];
    }
    parsed_batch.SetCount(merged_count);

    uint16_t sent = rte_eth_tx_burst(tx.port_id, tx.queue_id, parsed_batch.Data(),
                                     parsed_batch.Count());

    // Free untransmitted mbufs.
    rte_pktmbuf_free_bulk(parsed_batch.Data() + sent, parsed_batch.Count() - sent);

    // Release ownership so the Batch destructor doesn't double-free.
    parsed_batch.Release();
    flow_miss_batch.Release();
  }

  RefreshGcScheduling();
}

void FiveTupleForwardingProcessor::RefreshGcScheduling() {
  if (job_runner_ == nullptr || !gc_job_registered_) return;

  // Sync with auto-return: if job was returned to pending, update our flag.
  gc_job_scheduled_ = (flow_gc_job_.state() == PmdJob::State::kRunner);

  bool should_run = ShouldTriggerGc();
  if (should_run && !gc_job_scheduled_) {
    gc_job_scheduled_ = job_runner_->Schedule(&flow_gc_job_);
  } else if (!should_run && gc_job_scheduled_) {
    if (job_runner_->Unschedule(&flow_gc_job_)) {
      gc_job_scheduled_ = false;
    }
  }
}

bool FiveTupleForwardingProcessor::ShouldTriggerGc() const {
  return max_batch_count_ < kBatchSize / 2 &&
         table_.size() >= table_.capacity() / 2;
}

void FiveTupleForwardingProcessor::RunFlowGc(uint64_t /*now_tsc*/) {
  table_.EvictLru(kGcBatchSize);
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

void FiveTupleForwardingProcessor::RegisterControlCommands(
    dpdk_config::CommandRegistry& registry,
    const dpdk_config::ProcessorCommandRuntime& runtime) {
  registry.RegisterAsyncCommand(
      "get_flow_table", "five_tuple_forwarding",
      [runtime](const nlohmann::json& /*params*/,
                dpdk_config::CommandResultCallback done) mutable {
        if (!runtime.get_lcore_ids || !runtime.get_processor_data ||
            !runtime.call_after_grace_period) {
          done(dpdk_config::CommandResult::Error("not_supported"));
          return;
        }

        struct TableInfo {
          uint32_t lcore_id;
          FlowTable* table;
        };
        auto tables = std::make_shared<std::vector<TableInfo>>();

        for (uint32_t lcore_id : runtime.get_lcore_ids()) {
          auto* pmd = static_cast<PmdData*>(
              runtime.get_processor_data(lcore_id));
          FlowTable* tbl = pmd ? pmd->table : nullptr;
          tables->push_back({lcore_id, tbl});
          if (tbl != nullptr) {
            tbl->SetModifiable(false);
          }
        }

        auto restore_modifiable = [tables]() {
          for (const auto& info : *tables) {
            if (info.table != nullptr) {
              info.table->SetModifiable(true);
            }
          }
        };

        auto shared_done =
            std::make_shared<dpdk_config::CommandResultCallback>(
                std::move(done));

        auto status = runtime.call_after_grace_period(
            [tables, shared_done, restore_modifiable]() {
              nlohmann::json threads_array = nlohmann::json::array();

              try {
                for (const auto& info : *tables) {
                  nlohmann::json entries = nlohmann::json::array();
                  if (info.table != nullptr) {
                    info.table->ForEachEntry([&](rxtx::LookupEntry* e) {
                      entries.push_back(SerializeLookupEntry(*e));
                    });
                  }
                  threads_array.push_back(
                      {{"lcore_id", info.lcore_id}, {"entries", entries}});
                }
              } catch (const std::exception&) {
                restore_modifiable();
                (*shared_done)(dpdk_config::CommandResult::Error(
                    "Failed to serialize flow table entries"));
                return;
              }

              restore_modifiable();
              (*shared_done)(dpdk_config::CommandResult::Success(
                  {{"threads", threads_array}}));
            });

        if (!status.ok()) {
          restore_modifiable();
          (*shared_done)(dpdk_config::CommandResult::Error(
              "Failed to schedule grace period"));
        }
      });

  registry.RegisterAsyncCommand(
      "get_flow_table_count", "five_tuple_forwarding",
      [runtime](const nlohmann::json& /*params*/,
                dpdk_config::CommandResultCallback done) mutable {
        if (!runtime.get_lcore_ids || !runtime.get_processor_data ||
            !runtime.call_after_grace_period) {
          done(dpdk_config::CommandResult::Error("not_supported"));
          return;
        }

        struct TableInfo {
          uint32_t lcore_id;
          FlowTable* table;
        };
        auto tables = std::make_shared<std::vector<TableInfo>>();

        for (uint32_t lcore_id : runtime.get_lcore_ids()) {
          auto* pmd = static_cast<PmdData*>(
              runtime.get_processor_data(lcore_id));
          FlowTable* tbl = pmd ? pmd->table : nullptr;
          tables->push_back({lcore_id, tbl});
          if (tbl != nullptr) {
            tbl->SetModifiable(false);
          }
        }

        auto restore_modifiable = [tables]() {
          for (const auto& info : *tables) {
            if (info.table != nullptr) {
              info.table->SetModifiable(true);
            }
          }
        };

        auto shared_done =
            std::make_shared<dpdk_config::CommandResultCallback>(
                std::move(done));

        auto status = runtime.call_after_grace_period(
            [tables, shared_done, restore_modifiable]() {
              nlohmann::json threads_array = nlohmann::json::array();

              for (const auto& info : *tables) {
                size_t count = 0;
                if (info.table != nullptr) {
                  count = info.table->size();
                }
                threads_array.push_back(
                    {{"lcore_id", info.lcore_id}, {"count", count}});
              }

              restore_modifiable();
              (*shared_done)(dpdk_config::CommandResult::Success(
                  {{"threads", threads_array}}));
            });

        if (!status.ok()) {
          restore_modifiable();
          (*shared_done)(dpdk_config::CommandResult::Error(
              "Failed to schedule grace period"));
        }
      });

  registry.RegisterSyncCommand(
      "get_proc_stats", "five_tuple_forwarding",
      [runtime](const nlohmann::json& /*params*/)
          -> dpdk_config::CommandResult {
        if (!runtime.get_lcore_ids || !runtime.get_processor_data) {
          return dpdk_config::CommandResult::Error("not_supported");
        }

        nlohmann::json threads_array = nlohmann::json::array();
        uint64_t total_flow_misses = 0;
        uint64_t total_session_misses = 0;

        for (uint32_t lcore_id : runtime.get_lcore_ids()) {
          auto* pmd = static_cast<PmdData*>(
              runtime.get_processor_data(lcore_id));
          uint64_t fm = 0;
          uint64_t sm = 0;
          if (pmd != nullptr && pmd->stats != nullptr) {
            fm = pmd->stats->GetFlowTableMisses();
            sm = pmd->stats->GetSessionLookupMisses();
          }
          threads_array.push_back({{"lcore_id", lcore_id},
                                   {"flow_table_misses", fm},
                                   {"session_lookup_misses", sm}});
          total_flow_misses += fm;
          total_session_misses += sm;
        }

        return dpdk_config::CommandResult::Success(
            {{"threads", threads_array},
             {"total", {{"flow_table_misses", total_flow_misses},
                        {"session_lookup_misses", total_session_misses}}}});
      });
}

REGISTER_PROCESSOR("five_tuple_forwarding", FiveTupleForwardingProcessor);

}  // namespace processor
