#include "config/config_printer.h"
#include "config/config_parser.h"

#include <iostream>
#include <string>

using namespace dpdk_config;

// Simple test helper
int failed_tests = 0;

void TestCase(const std::string& name, bool condition) {
  if (condition) {
    std::cout << "[PASS] " << name << "\n";
  } else {
    std::cout << "[FAIL] " << name << "\n";
    failed_tests++;
  }
}

int main() {
  std::cout << "Running ConfigPrinter tests...\n\n";

  // Test 1: Empty configuration
  {
    DpdkConfig config;
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Empty config produces valid JSON", json == "{}");
  }

  // Test 2: Configuration with core_mask
  {
    DpdkConfig config;
    config.core_mask = "0xff";
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with core_mask contains field", 
             json.find("\"core_mask\"") != std::string::npos &&
             json.find("\"0xff\"") != std::string::npos);
  }

  // Test 3: Configuration with memory_channels
  {
    DpdkConfig config;
    config.memory_channels = 4;
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with memory_channels contains field",
             json.find("\"memory_channels\"") != std::string::npos &&
             json.find("4") != std::string::npos);
  }

  // Test 4: Configuration with PCI allowlist
  {
    DpdkConfig config;
    config.pci_allowlist.push_back("0000:01:00.0");
    config.pci_allowlist.push_back("0000:01:00.1");
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with pci_allowlist contains array",
             json.find("\"pci_allowlist\"") != std::string::npos &&
             json.find("0000:01:00.0") != std::string::npos &&
             json.find("0000:01:00.1") != std::string::npos);
  }

  // Test 5: Configuration with PCI blocklist
  {
    DpdkConfig config;
    config.pci_blocklist.push_back("0000:02:00.0");
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with pci_blocklist contains array",
             json.find("\"pci_blocklist\"") != std::string::npos &&
             json.find("0000:02:00.0") != std::string::npos);
  }

  // Test 6: Configuration with log_level
  {
    DpdkConfig config;
    config.log_level = 7;
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with log_level contains field",
             json.find("\"log_level\"") != std::string::npos &&
             json.find("7") != std::string::npos);
  }

  // Test 7: Configuration with huge_pages
  {
    DpdkConfig config;
    config.huge_pages = 1024;
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with huge_pages contains field",
             json.find("\"huge_pages\"") != std::string::npos &&
             json.find("1024") != std::string::npos);
  }

  // Test 8: Complete configuration
  {
    DpdkConfig config;
    config.core_mask = "0xff";
    config.memory_channels = 4;
    config.pci_allowlist.push_back("0000:01:00.0");
    config.pci_allowlist.push_back("0000:01:00.1");
    config.pci_blocklist.push_back("0000:02:00.0");
    config.log_level = 7;
    config.huge_pages = 1024;
    
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Complete config contains all fields",
             json.find("\"core_mask\"") != std::string::npos &&
             json.find("\"memory_channels\"") != std::string::npos &&
             json.find("\"pci_allowlist\"") != std::string::npos &&
             json.find("\"pci_blocklist\"") != std::string::npos &&
             json.find("\"log_level\"") != std::string::npos &&
             json.find("\"huge_pages\"") != std::string::npos);
  }

  // Test 9: Indentation parameter
  {
    DpdkConfig config;
    config.core_mask = "0xff";
    
    std::string json_default = ConfigPrinter::ToJson(config);
    std::string json_indent4 = ConfigPrinter::ToJson(config, 4);
    
    TestCase("Different indentation produces different output",
             json_default != json_indent4);
  }

  // Test 10: Round-trip test (parse -> print -> parse)
  {
    std::string original_json = R"({
  "core_mask": "0xff",
  "memory_channels": 4,
  "pci_allowlist": ["0000:01:00.0", "0000:01:00.1"],
  "pci_blocklist": ["0000:02:00.0"],
  "log_level": 7,
  "huge_pages": 1024
})";
    
    auto config_or = ConfigParser::ParseString(original_json);
    if (!config_or.ok()) {
      TestCase("Round-trip: parse original", false);
    } else {
      std::string printed_json = ConfigPrinter::ToJson(*config_or);
      auto config2_or = ConfigParser::ParseString(printed_json);
      
      if (!config2_or.ok()) {
        TestCase("Round-trip: parse printed", false);
      } else {
        // Compare configurations
        bool same = 
          config_or->core_mask == config2_or->core_mask &&
          config_or->memory_channels == config2_or->memory_channels &&
          config_or->pci_allowlist == config2_or->pci_allowlist &&
          config_or->pci_blocklist == config2_or->pci_blocklist &&
          config_or->log_level == config2_or->log_level &&
          config_or->huge_pages == config2_or->huge_pages;
        
        TestCase("Round-trip: parse -> print -> parse preserves data", same);
      }
    }
  }

  // Test 11: Additional params
  {
    DpdkConfig config;
    config.additional_params.push_back({"custom_param", "value123"});
    config.additional_params.push_back({"another_param", "42"});
    
    std::string json = ConfigPrinter::ToJson(config);
    TestCase("Config with additional_params contains fields",
             json.find("\"custom_param\"") != std::string::npos &&
             json.find("\"another_param\"") != std::string::npos);
  }

  std::cout << "\nAll tests completed.\n";
  return failed_tests > 0 ? 1 : 0;
}
