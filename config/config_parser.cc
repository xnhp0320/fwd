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

  // Parse additional_params (any other fields as key-value pairs)
  // Skip the known fields we've already processed
  const std::set<std::string> known_fields = {
      "core_mask", "memory_channels", "pci_allowlist",
      "pci_blocklist", "log_level", "huge_pages"
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
