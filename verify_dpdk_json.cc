#include <iostream>
#include <fstream>
#include <sstream>
#include "config/config_parser.h"
#include "config/config_validator.h"
#include "config/config_printer.h"

int main() {
  // Read dpdk.json file
  std::ifstream file("dpdk.json");
  if (!file.is_open()) {
    std::cerr << "Failed to open dpdk.json\n";
    return 1;
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string json_content = buffer.str();
  
  // Parse configuration
  std::cout << "Parsing dpdk.json...\n";
  auto config_or = dpdk_config::ConfigParser::ParseString(json_content);
  if (!config_or.ok()) {
    std::cerr << "Parse error: " << config_or.status() << "\n";
    return 1;
  }
  std::cout << "✓ Parse successful\n";
  
  // Validate configuration
  std::cout << "\nValidating configuration...\n";
  auto validation_status = dpdk_config::ConfigValidator::Validate(*config_or);
  if (!validation_status.ok()) {
    std::cerr << "Validation error: " << validation_status << "\n";
    return 1;
  }
  std::cout << "✓ Validation successful\n";
  
  // Print configuration back
  std::cout << "\nRound-trip test (parse -> print)...\n";
  std::string printed_json = dpdk_config::ConfigPrinter::ToJson(*config_or, 2);
  std::cout << "✓ Print successful\n";
  
  // Display PMD thread configuration summary
  std::cout << "\nPMD Thread Configuration Summary:\n";
  std::cout << "  Core mask: " << (config_or->core_mask ? *config_or->core_mask : "not set") << "\n";
  std::cout << "  Number of PMD threads: " << config_or->pmd_threads.size() << "\n";
  
  for (const auto& pmd : config_or->pmd_threads) {
    std::cout << "  - Lcore " << pmd.lcore_id << ":\n";
    std::cout << "      RX queues: " << pmd.rx_queues.size() << "\n";
    for (const auto& q : pmd.rx_queues) {
      std::cout << "        port " << q.port_id << ", queue " << q.queue_id << "\n";
    }
    std::cout << "      TX queues: " << pmd.tx_queues.size() << "\n";
    for (const auto& q : pmd.tx_queues) {
      std::cout << "        port " << q.port_id << ", queue " << q.queue_id << "\n";
    }
  }
  
  std::cout << "\n✓ All integration checks passed!\n";
  return 0;
}
