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

  // Serialize additional_params (key-value pairs)
  for (const auto& [key, value] : config.additional_params) {
    // Try to parse value as JSON to preserve original type
    try {
      j[key] = json::parse(value);
    } catch (const json::parse_error&) {
      // If parsing fails, treat as string
      j[key] = value;
    }
  }

  // Format with specified indentation
  return j.dump(indent);
}

}  // namespace dpdk_config
