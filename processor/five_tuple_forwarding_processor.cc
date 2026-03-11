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

void FiveTupleForwardingProcessor::process_impl() {
  max_batch_count_ = 0;

  const auto& tx = config().tx_queues[0];

  for (const auto& rx : config().rx_queues) {
    rxtx::Batch<kBatchSize> batch;
    batch.SetCount(rte_eth_rx_burst(rx.port_id, rx.queue_id, batch.Data(),
                                    batch.Capacity()));

    // Note: some NIC PMD drivers with limited burst sizes (e.g., virtio, tap)
    // may return fewer packets per burst even under load, which can cause false
    // light-traffic classification.
    max_batch_count_ = std::max(max_batch_count_, batch.Count());

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
          proc_stats_.RecordSessionLookupMiss();
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
        proc_stats_.RecordFlowTableMiss();
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
          rxtx::FastLookupTable<>* table;
        };
        auto tables = std::make_shared<std::vector<TableInfo>>();

        for (uint32_t lcore_id : runtime.get_lcore_ids()) {
          auto* pmd = static_cast<PmdData*>(
              runtime.get_processor_data(lcore_id));
          rxtx::FastLookupTable<>* tbl = pmd ? pmd->table : nullptr;
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
                    for (auto it = info.table->Begin();
                         it != info.table->End(); ++it) {
                      entries.push_back(SerializeLookupEntry(**it));
                    }
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
          rxtx::FastLookupTable<>* table;
        };
        auto tables = std::make_shared<std::vector<TableInfo>>();

        for (uint32_t lcore_id : runtime.get_lcore_ids()) {
          auto* pmd = static_cast<PmdData*>(
              runtime.get_processor_data(lcore_id));
          rxtx::FastLookupTable<>* tbl = pmd ? pmd->table : nullptr;
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
                size_t count;
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
