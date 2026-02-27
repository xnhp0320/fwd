#include "config/config_parser.h"

#include <fstream>
#include <set>
#include <sstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

namespace dpdk_config {

using json = nlohmann::json;

absl::StatusOr<DpdkConfig> ConfigParser::ParseFile(
    const std::string& file_path) {
  // Open the file
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Configuration file not found: ", file_path));
  }

  // Read file contents
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  // Check for empty file
  if (content.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Configuration file is empty: ", file_path));
  }

  // Delegate to ParseString
  return ParseString(content);
}

absl::StatusOr<DpdkConfig> ConfigParser::ParseString(
    const std::string& json_content) {
  // Check for empty content
  if (json_content.empty()) {
    return absl::InvalidArgumentError("Configuration content is empty");
  }

  // Parse JSON
  json j;
  try {
    j = json::parse(json_content);
  } catch (const json::parse_error& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("JSON parse error at byte ", e.byte, ": ", e.what()));
  }

  // Ensure root is an object
  if (!j.is_object()) {
    return absl::InvalidArgumentError(
        "Configuration must be a JSON object");
  }

  // Parse into DpdkConfig structure
  DpdkConfig config;

  // Parse core_mask (optional string)
  if (j.contains("core_mask")) {
    if (!j["core_mask"].is_string()) {
      return absl::InvalidArgumentError(
          "Field 'core_mask' must be a string");
    }
    config.core_mask = j["core_mask"].get<std::string>();
  }

  // Parse memory_channels (optional integer)
  if (j.contains("memory_channels")) {
    if (!j["memory_channels"].is_number_integer()) {
      return absl::InvalidArgumentError(
          "Field 'memory_channels' must be an integer");
    }
    config.memory_channels = j["memory_channels"].get<int>();
  }

  // Parse pci_allowlist (optional array of strings)
  if (j.contains("pci_allowlist")) {
    if (!j["pci_allowlist"].is_array()) {
      return absl::InvalidArgumentError(
          "Field 'pci_allowlist' must be an array");
    }
    for (const auto& item : j["pci_allowlist"]) {
      if (!item.is_string()) {
        return absl::InvalidArgumentError(
            "All elements in 'pci_allowlist' must be strings");
      }
      config.pci_allowlist.push_back(item.get<std::string>());
    }
  }

  // Parse pci_blocklist (optional array of strings)
  if (j.contains("pci_blocklist")) {
    if (!j["pci_blocklist"].is_array()) {
      return absl::InvalidArgumentError(
          "Field 'pci_blocklist' must be an array");
    }
    for (const auto& item : j["pci_blocklist"]) {
      if (!item.is_string()) {
        return absl::InvalidArgumentError(
            "All elements in 'pci_blocklist' must be strings");
      }
      config.pci_blocklist.push_back(item.get<std::string>());
    }
  }

  // Parse log_level (optional integer)
  if (j.contains("log_level")) {
    if (!j["log_level"].is_number_integer()) {
      return absl::InvalidArgumentError(
          "Field 'log_level' must be an integer");
    }
    config.log_level = j["log_level"].get<int>();
  }

  // Parse huge_pages (optional integer)
  if (j.contains("huge_pages")) {
    if (!j["huge_pages"].is_number_integer()) {
      return absl::InvalidArgumentError(
          "Field 'huge_pages' must be an integer");
    }
    config.huge_pages = j["huge_pages"].get<int>();
  }

  // Parse ports array (optional array of port configurations)
  if (j.contains("ports")) {
    if (!j["ports"].is_array()) {
      return absl::InvalidArgumentError(
          "Field 'ports' must be an array");
    }
    
    for (const auto& port_json : j["ports"]) {
      if (!port_json.is_object()) {
        return absl::InvalidArgumentError(
            "Each element in 'ports' array must be an object");
      }
      
      DpdkPortConfig port_config;
      
      // Parse port_id (required)
      if (!port_json.contains("port_id")) {
        return absl::InvalidArgumentError(
            "Port configuration missing required field: port_id");
      }
      if (!port_json["port_id"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            "Field 'port_id' must be an unsigned integer");
      }
      port_config.port_id = port_json["port_id"].get<uint16_t>();
      
      // Parse num_rx_queues (required)
      if (!port_json.contains("num_rx_queues")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         " missing required field: num_rx_queues"));
      }
      if (!port_json["num_rx_queues"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         ": field 'num_rx_queues' must be an unsigned integer"));
      }
      port_config.num_rx_queues = port_json["num_rx_queues"].get<uint16_t>();
      
      // Parse num_tx_queues (required)
      if (!port_json.contains("num_tx_queues")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         " missing required field: num_tx_queues"));
      }
      if (!port_json["num_tx_queues"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         ": field 'num_tx_queues' must be an unsigned integer"));
      }
      port_config.num_tx_queues = port_json["num_tx_queues"].get<uint16_t>();
      
      // Parse num_descriptors (required)
      if (!port_json.contains("num_descriptors")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         " missing required field: num_descriptors"));
      }
      if (!port_json["num_descriptors"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         ": field 'num_descriptors' must be an unsigned integer"));
      }
      port_config.num_descriptors = port_json["num_descriptors"].get<uint16_t>();
      
      // Parse mbuf_pool_size (required)
      if (!port_json.contains("mbuf_pool_size")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         " missing required field: mbuf_pool_size"));
      }
      if (!port_json["mbuf_pool_size"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         ": field 'mbuf_pool_size' must be an unsigned integer"));
      }
      port_config.mbuf_pool_size = port_json["mbuf_pool_size"].get<uint32_t>();
      
      // Parse mbuf_size (required)
      if (!port_json.contains("mbuf_size")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         " missing required field: mbuf_size"));
      }
      if (!port_json["mbuf_size"].is_number_unsigned()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, 
                         ": field 'mbuf_size' must be an unsigned integer"));
      }
      port_config.mbuf_size = port_json["mbuf_size"].get<uint16_t>();
      
      // Add the parsed port configuration to the config
      config.ports.push_back(port_config);
    }
  }

  // Parse additional_params (any other fields as key-value pairs)
  // Skip the known fields we've already processed
  const std::set<std::string> known_fields = {
      "core_mask", "memory_channels", "pci_allowlist",
      "pci_blocklist", "log_level", "huge_pages", "ports"
  };

  for (auto it = j.begin(); it != j.end(); ++it) {
    if (known_fields.find(it.key()) == known_fields.end()) {
      // Convert value to string for additional_params
      std::string value_str;
      if (it.value().is_string()) {
        value_str = it.value().get<std::string>();
      } else {
        value_str = it.value().dump();
      }
      config.additional_params.emplace_back(it.key(), value_str);
    }
  }

  return config;
}

}  // namespace dpdk_config
