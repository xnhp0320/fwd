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

  // Test port configuration validation
  {
    DpdkConfig config;
    DpdkPortConfig port;
    
    // Valid port configuration
    port.port_id = 0;
    port.num_rx_queues = 4;
    port.num_tx_queues = 4;
    port.num_descriptors = 1024;
    port.mbuf_pool_size = 16384;
    port.mbuf_size = 2048;
    config.ports.push_back(port);
    TestCase("Valid port configuration", ConfigValidator::Validate(config).ok());
    
    // Test duplicate port IDs
    DpdkConfig config2;
    DpdkPortConfig port1;
    port1.port_id = 0;
    port1.num_rx_queues = 4;
    port1.num_tx_queues = 4;
    port1.num_descriptors = 1024;
    port1.mbuf_pool_size = 16384;
    port1.mbuf_size = 2048;
    config2.ports.push_back(port1);
    
    DpdkPortConfig port2 = port1;
    port2.port_id = 0;  // Duplicate
    config2.ports.push_back(port2);
    TestCase("Invalid - duplicate port IDs", !ConfigValidator::Validate(config2).ok());
    
    // Test num_rx_queues = 0
    DpdkConfig config3;
    DpdkPortConfig port3;
    port3.port_id = 0;
    port3.num_rx_queues = 0;  // Invalid
    port3.num_tx_queues = 4;
    port3.num_descriptors = 1024;
    port3.mbuf_pool_size = 16384;
    port3.mbuf_size = 2048;
    config3.ports.push_back(port3);
    TestCase("Invalid - num_rx_queues = 0", !ConfigValidator::Validate(config3).ok());
    
    // Test num_tx_queues = 0
    DpdkConfig config4;
    DpdkPortConfig port4;
    port4.port_id = 0;
    port4.num_rx_queues = 4;
    port4.num_tx_queues = 0;  // Invalid
    port4.num_descriptors = 1024;
    port4.mbuf_pool_size = 16384;
    port4.mbuf_size = 2048;
    config4.ports.push_back(port4);
    TestCase("Invalid - num_tx_queues = 0", !ConfigValidator::Validate(config4).ok());
    
    // Test num_descriptors not power of 2
    DpdkConfig config5;
    DpdkPortConfig port5;
    port5.port_id = 0;
    port5.num_rx_queues = 4;
    port5.num_tx_queues = 4;
    port5.num_descriptors = 1000;  // Not power of 2
    port5.mbuf_pool_size = 16384;
    port5.mbuf_size = 2048;
    config5.ports.push_back(port5);
    TestCase("Invalid - num_descriptors not power of 2", !ConfigValidator::Validate(config5).ok());
    
    // Test valid power of 2 descriptors
    DpdkConfig config6;
    DpdkPortConfig port6;
    port6.port_id = 0;
    port6.num_rx_queues = 4;
    port6.num_tx_queues = 4;
    port6.num_descriptors = 512;  // Valid power of 2
    port6.mbuf_pool_size = 16384;
    port6.mbuf_size = 2048;
    config6.ports.push_back(port6);
    TestCase("Valid - num_descriptors = 512 (power of 2)", ConfigValidator::Validate(config6).ok());
    
    // Test mbuf_pool_size = 0
    DpdkConfig config7;
    DpdkPortConfig port7;
    port7.port_id = 0;
    port7.num_rx_queues = 4;
    port7.num_tx_queues = 4;
    port7.num_descriptors = 1024;
    port7.mbuf_pool_size = 0;  // Invalid
    port7.mbuf_size = 2048;
    config7.ports.push_back(port7);
    TestCase("Invalid - mbuf_pool_size = 0", !ConfigValidator::Validate(config7).ok());
    
    // Test mbuf_size = 0
    DpdkConfig config8;
    DpdkPortConfig port8;
    port8.port_id = 0;
    port8.num_rx_queues = 4;
    port8.num_tx_queues = 4;
    port8.num_descriptors = 1024;
    port8.mbuf_pool_size = 16384;
    port8.mbuf_size = 0;  // Invalid
    config8.ports.push_back(port8);
    TestCase("Invalid - mbuf_size = 0", !ConfigValidator::Validate(config8).ok());
    
    // Test multiple valid ports with different IDs
    DpdkConfig config9;
    DpdkPortConfig portA;
    portA.port_id = 0;
    portA.num_rx_queues = 4;
    portA.num_tx_queues = 4;
    portA.num_descriptors = 1024;
    portA.mbuf_pool_size = 16384;
    portA.mbuf_size = 2048;
    config9.ports.push_back(portA);
    
    DpdkPortConfig portB;
    portB.port_id = 1;
    portB.num_rx_queues = 2;
    portB.num_tx_queues = 2;
    portB.num_descriptors = 512;
    portB.mbuf_pool_size = 8192;
    portB.mbuf_size = 9216;
    config9.ports.push_back(portB);
    TestCase("Valid - multiple ports with different IDs", ConfigValidator::Validate(config9).ok());
    
    // Test warning for low mbuf_pool_size (should still pass validation)
    std::cout << "\n--- Testing warning for low mbuf_pool_size ---\n";
    DpdkConfig config10;
    DpdkPortConfig port10;
    port10.port_id = 0;
    port10.num_rx_queues = 4;
    port10.num_tx_queues = 4;
    port10.num_descriptors = 1024;
    port10.mbuf_pool_size = 1000;  // Below recommended: 1024 * 8 + 512 = 8704
    port10.mbuf_size = 2048;
    config10.ports.push_back(port10);
    TestCase("Valid but warns - low mbuf_pool_size", ConfigValidator::Validate(config10).ok());
    std::cout << "--- End of warning test ---\n\n";
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
