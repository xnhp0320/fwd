#include "config/config_validator.h"

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
  std::cout << "Running ConfigValidator tests...\n\n";

  // Test IsValidHexString
  {
    DpdkConfig config;
    
    // Valid hex strings
    config.core_mask = "0xff";
    TestCase("Valid hex with 0x prefix", ConfigValidator::Validate(config).ok());
    
    config.core_mask = "FF";
    TestCase("Valid hex uppercase", ConfigValidator::Validate(config).ok());
    
    config.core_mask = "0x1234abcd";
    TestCase("Valid hex mixed case", ConfigValidator::Validate(config).ok());
    
    config.core_mask = "ABCDEF";
    TestCase("Valid hex all uppercase", ConfigValidator::Validate(config).ok());
    
    // Invalid hex strings
    config.core_mask = "0xGG";
    TestCase("Invalid hex with G", !ConfigValidator::Validate(config).ok());
    
    config.core_mask = "xyz";
    TestCase("Invalid hex with xyz", !ConfigValidator::Validate(config).ok());
    
    config.core_mask = "0x";
    TestCase("Invalid hex - only prefix", !ConfigValidator::Validate(config).ok());
    
    config.core_mask = "";
    TestCase("Invalid hex - empty string", !ConfigValidator::Validate(config).ok());
  }

  // Test memory_channels validation
  {
    DpdkConfig config;
    
    config.memory_channels = 4;
    TestCase("Valid memory_channels positive", ConfigValidator::Validate(config).ok());
    
    config.memory_channels = 0;
    TestCase("Invalid memory_channels zero", !ConfigValidator::Validate(config).ok());
    
    config.memory_channels = -1;
    TestCase("Invalid memory_channels negative", !ConfigValidator::Validate(config).ok());
  }

  // Test PCI address validation
  {
    DpdkConfig config;
    
    config.pci_allowlist.push_back("0000:01:00.0");
    TestCase("Valid PCI address", ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("FFFF:FF:FF.F");
    TestCase("Valid PCI address all F", ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("1234:5a:bc.d");
    TestCase("Valid PCI address mixed case", ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("123:01:00.0");
    TestCase("Invalid PCI - short domain", !ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("0000:1:00.0");
    TestCase("Invalid PCI - short bus", !ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("0000:01:0.0");
    TestCase("Invalid PCI - short device", !ConfigValidator::Validate(config).ok());
    
    config.pci_allowlist.clear();
    config.pci_allowlist.push_back("0000:01:00.FF");
    TestCase("Invalid PCI - long function", !ConfigValidator::Validate(config).ok());
  }

  // Test log_level validation
  {
    DpdkConfig config;
    
    config.log_level = 0;
    TestCase("Valid log_level 0", ConfigValidator::Validate(config).ok());
    
    config.log_level = 8;
    TestCase("Valid log_level 8", ConfigValidator::Validate(config).ok());
    
    config.log_level = 4;
    TestCase("Valid log_level 4", ConfigValidator::Validate(config).ok());
    
    config.log_level = -1;
    TestCase("Invalid log_level negative", !ConfigValidator::Validate(config).ok());
    
    config.log_level = 9;
    TestCase("Invalid log_level too high", !ConfigValidator::Validate(config).ok());
  }

  // Test huge_pages validation
  {
    DpdkConfig config;
    
    config.huge_pages = 1024;
    TestCase("Valid huge_pages positive", ConfigValidator::Validate(config).ok());
    
    config.huge_pages = 0;
    TestCase("Invalid huge_pages zero", !ConfigValidator::Validate(config).ok());
    
    config.huge_pages = -1;
    TestCase("Invalid huge_pages negative", !ConfigValidator::Validate(config).ok());
  }

  // Test PCI allowlist/blocklist conflict
  {
    DpdkConfig config;
    
    config.pci_allowlist.push_back("0000:01:00.0");
    config.pci_blocklist.push_back("0000:02:00.0");
    TestCase("No conflict - different addresses", ConfigValidator::Validate(config).ok());
    
    config.pci_blocklist.push_back("0000:01:00.0");
    TestCase("Conflict - same address in both lists", !ConfigValidator::Validate(config).ok());
  }

  // Test valid complete configuration
  {
    DpdkConfig config;
    config.core_mask = "0xff";
    config.memory_channels = 4;
    config.pci_allowlist.push_back("0000:01:00.0");
    config.pci_allowlist.push_back("0000:01:00.1");
    config.pci_blocklist.push_back("0000:02:00.0");
    config.log_level = 7;
    config.huge_pages = 1024;
    
    TestCase("Valid complete configuration", ConfigValidator::Validate(config).ok());
  }

  std::cout << "\nAll tests completed.\n";
  return failed_tests > 0 ? 1 : 0;
}
