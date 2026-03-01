#include "config/config_printer.h"

#include "nlohmann/json.hpp"

namespace dpdk_config {

using json = nlohmann::json;

std::string ConfigPrinter::ToJson(const DpdkConfig& config, int indent) {
  json j = json::object();  // Initialize as empty object instead of null

  // Serialize core_mask (optional string)
  if (config.core_mask.has_value()) {
    j["core_mask"] = *config.core_mask;
  }

  // Serialize memory_channels (optional integer)
  if (config.memory_channels.has_value()) {
    j["memory_channels"] = *config.memory_channels;
  }

  // Serialize pci_allowlist (array of strings)
  if (!config.pci_allowlist.empty()) {
    j["pci_allowlist"] = config.pci_allowlist;
  }

  // Serialize pci_blocklist (array of strings)
  if (!config.pci_blocklist.empty()) {
    j["pci_blocklist"] = config.pci_blocklist;
  }

  // Serialize log_level (optional integer)
  if (config.log_level.has_value()) {
    j["log_level"] = *config.log_level;
  }

  // Serialize huge_pages (optional integer)
  if (config.huge_pages.has_value()) {
    j["huge_pages"] = *config.huge_pages;
  }

  // Serialize ports (array of port configurations)
  if (!config.ports.empty()) {
    json ports_array = json::array();
    
    for (const auto& port : config.ports) {
      json port_json;
      port_json["port_id"] = port.port_id;
      port_json["num_rx_queues"] = port.num_rx_queues;
      port_json["num_tx_queues"] = port.num_tx_queues;
      port_json["num_descriptors"] = port.num_descriptors;
      port_json["mbuf_pool_size"] = port.mbuf_pool_size;
      port_json["mbuf_size"] = port.mbuf_size;
      
      ports_array.push_back(port_json);
    }
    
    j["ports"] = ports_array;
  }

  // Serialize pmd_threads (array of PMD thread configurations)
  if (!config.pmd_threads.empty()) {
    json pmd_threads_array = json::array();
    
    for (const auto& pmd_config : config.pmd_threads) {
      json thread_json;
      thread_json["lcore_id"] = pmd_config.lcore_id;
      
      // Serialize rx_queues (skip if empty)
      if (!pmd_config.rx_queues.empty()) {
        json rx_queues_array = json::array();
        for (const auto& queue : pmd_config.rx_queues) {
          json queue_json;
          queue_json["port_id"] = queue.port_id;
          queue_json["queue_id"] = queue.queue_id;
          rx_queues_array.push_back(queue_json);
        }
        thread_json["rx_queues"] = rx_queues_array;
      }
      
      // Serialize tx_queues (skip if empty)
      if (!pmd_config.tx_queues.empty()) {
        json tx_queues_array = json::array();
        for (const auto& queue : pmd_config.tx_queues) {
          json queue_json;
          queue_json["port_id"] = queue.port_id;
          queue_json["queue_id"] = queue.queue_id;
          tx_queues_array.push_back(queue_json);
        }
        thread_json["tx_queues"] = tx_queues_array;
      }

      // Serialize processor_name (skip if empty — empty means use default)
      if (!pmd_config.processor_name.empty()) {
        thread_json["processor"] = pmd_config.processor_name;
      }
      
      pmd_threads_array.push_back(thread_json);
    }
    
    j["pmd_threads"] = pmd_threads_array;
  }

  // Serialize additional_params as array of [key, value] pairs
  if (!config.additional_params.empty()) {
    json additional_params_array = json::array();
    for (const auto& [key, value] : config.additional_params) {
      json param_pair = json::array();
      param_pair.push_back(key);
      
      // Try to parse value as JSON to preserve original type
      try {
        param_pair.push_back(json::parse(value));
      } catch (const json::parse_error&) {
        // If parsing fails, treat as string
        param_pair.push_back(value);
      }
      
      additional_params_array.push_back(param_pair);
    }
    j["additional_params"] = additional_params_array;
  }

  // Format with specified indentation
  return j.dump(indent);
}

}  // namespace dpdk_config
