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

  // Test ParseCoremask function
  {
    std::cout << "\n--- Testing ParseCoremask function ---\n";
    
    // Test with 0x prefix
    auto lcores1 = ConfigValidator::ParseCoremask(std::string("0xff"));
    TestCase("ParseCoremask 0xff - size 8", lcores1.size() == 8);
    TestCase("ParseCoremask 0xff - contains 0", lcores1.count(0) == 1);
    TestCase("ParseCoremask 0xff - contains 7", lcores1.count(7) == 1);
    TestCase("ParseCoremask 0xff - not contains 8", lcores1.count(8) == 0);
    
    // Test without 0x prefix
    auto lcores2 = ConfigValidator::ParseCoremask(std::string("ff"));
    TestCase("ParseCoremask ff - size 8", lcores2.size() == 8);
    TestCase("ParseCoremask ff - contains 0", lcores2.count(0) == 1);
    
    // Test uppercase
    auto lcores3 = ConfigValidator::ParseCoremask(std::string("0xFF"));
    TestCase("ParseCoremask 0xFF - size 8", lcores3.size() == 8);
    
    // Test single bit
    auto lcores4 = ConfigValidator::ParseCoremask(std::string("0x1"));
    TestCase("ParseCoremask 0x1 - size 1", lcores4.size() == 1);
    TestCase("ParseCoremask 0x1 - contains 0", lcores4.count(0) == 1);
    
    // Test multiple non-contiguous bits
    auto lcores5 = ConfigValidator::ParseCoremask(std::string("0x5"));  // 0101 binary
    TestCase("ParseCoremask 0x5 - size 2", lcores5.size() == 2);
    TestCase("ParseCoremask 0x5 - contains 0", lcores5.count(0) == 1);
    TestCase("ParseCoremask 0x5 - contains 2", lcores5.count(2) == 1);
    TestCase("ParseCoremask 0x5 - not contains 1", lcores5.count(1) == 0);
    
    // Test larger mask
    auto lcores6 = ConfigValidator::ParseCoremask(std::string("0xf0f"));  // 111100001111 binary
    TestCase("ParseCoremask 0xf0f - size 8", lcores6.size() == 8);
    TestCase("ParseCoremask 0xf0f - contains 0", lcores6.count(0) == 1);
    TestCase("ParseCoremask 0xf0f - contains 3", lcores6.count(3) == 1);
    TestCase("ParseCoremask 0xf0f - not contains 4", lcores6.count(4) == 0);
    TestCase("ParseCoremask 0xf0f - contains 8", lcores6.count(8) == 1);
    TestCase("ParseCoremask 0xf0f - contains 11", lcores6.count(11) == 1);
    
    // Test empty/null
    auto lcores7 = ConfigValidator::ParseCoremask(std::nullopt);
    TestCase("ParseCoremask nullopt - empty", lcores7.empty());
    
    auto lcores8 = ConfigValidator::ParseCoremask(std::string(""));
    TestCase("ParseCoremask empty string - empty", lcores8.empty());
    
    // Test 64-bit mask (high bits)
    auto lcores9 = ConfigValidator::ParseCoremask(std::string("0x8000000000000000"));
    TestCase("ParseCoremask 64-bit high - size 1", lcores9.size() == 1);
    TestCase("ParseCoremask 64-bit high - contains 63", lcores9.count(63) == 1);
    
    // Test all 64 bits
    auto lcores10 = ConfigValidator::ParseCoremask(std::string("0xFFFFFFFFFFFFFFFF"));
    TestCase("ParseCoremask all 64 bits - size 64", lcores10.size() == 64);
    TestCase("ParseCoremask all 64 bits - contains 0", lcores10.count(0) == 1);
    TestCase("ParseCoremask all 64 bits - contains 63", lcores10.count(63) == 1);
    
    std::cout << "--- End of ParseCoremask tests ---\n\n";
  }

  // Test DetermineMainLcore function
  {
    std::cout << "\n--- Testing DetermineMainLcore function ---\n";
    
    // Test with 0xff - should return 0 (lowest bit)
    uint32_t main1 = ConfigValidator::DetermineMainLcore(std::string("0xff"));
    TestCase("DetermineMainLcore 0xff - returns 0", main1 == 0);
    
    // Test with 0x06 (binary 0110) - should return 1 (lowest bit set)
    uint32_t main2 = ConfigValidator::DetermineMainLcore(std::string("0x06"));
    TestCase("DetermineMainLcore 0x06 - returns 1", main2 == 1);
    
    // Test with 0x04 (binary 0100) - should return 2
    uint32_t main3 = ConfigValidator::DetermineMainLcore(std::string("0x04"));
    TestCase("DetermineMainLcore 0x04 - returns 2", main3 == 2);
    
    // Test with 0xf0 (binary 11110000) - should return 4
    uint32_t main4 = ConfigValidator::DetermineMainLcore(std::string("0xf0"));
    TestCase("DetermineMainLcore 0xf0 - returns 4", main4 == 4);
    
    // Test with single bit at position 10
    uint32_t main5 = ConfigValidator::DetermineMainLcore(std::string("0x400"));
    TestCase("DetermineMainLcore 0x400 - returns 10", main5 == 10);
    
    // Test with empty/null - should return 0 as default
    uint32_t main6 = ConfigValidator::DetermineMainLcore(std::nullopt);
    TestCase("DetermineMainLcore nullopt - returns 0", main6 == 0);
    
    uint32_t main7 = ConfigValidator::DetermineMainLcore(std::string(""));
    TestCase("DetermineMainLcore empty string - returns 0", main7 == 0);
    
    // Test with high bit set (bit 63)
    uint32_t main8 = ConfigValidator::DetermineMainLcore(std::string("0x8000000000000001"));
    TestCase("DetermineMainLcore with bits 0 and 63 - returns 0", main8 == 0);
    
    // Test with only high bit set (bit 63)
    uint32_t main9 = ConfigValidator::DetermineMainLcore(std::string("0x8000000000000000"));
    TestCase("DetermineMainLcore with only bit 63 - returns 63", main9 == 63);
    
    // Test with non-contiguous bits (0x5 = binary 0101)
    uint32_t main10 = ConfigValidator::DetermineMainLcore(std::string("0x5"));
    TestCase("DetermineMainLcore 0x5 - returns 0", main10 == 0);
    
    // Test with 0xf0f (bits 0-3 and 8-11 set)
    uint32_t main11 = ConfigValidator::DetermineMainLcore(std::string("0xf0f"));
    TestCase("DetermineMainLcore 0xf0f - returns 0", main11 == 0);
    
    std::cout << "--- End of DetermineMainLcore tests ---\n\n";
  }

  // Test worker lcore availability check
  {
    std::cout << "\n--- Testing worker lcore availability check ---\n";
    
    // Test with coremask that has only main lcore (0x01 = only bit 0)
    DpdkConfig config1;
    config1.core_mask = "0x01";
    PmdThreadConfig pmd1;
    pmd1.lcore_id = 1;  // Try to use lcore 1, but it doesn't exist
    config1.pmd_threads.push_back(pmd1);
    TestCase("No worker lcores - coremask 0x01 with pmd_threads", 
             !ConfigValidator::Validate(config1).ok());
    
    // Test with coremask that has multiple lcores (0x03 = bits 0 and 1)
    DpdkConfig config2;
    config2.core_mask = "0x03";
    PmdThreadConfig pmd2;
    pmd2.lcore_id = 1;  // Use lcore 1 (worker lcore)
    config2.pmd_threads.push_back(pmd2);
    TestCase("Worker lcore available - coremask 0x03 with pmd_threads", 
             ConfigValidator::Validate(config2).ok());
    
    // Test with coremask 0xff (8 cores, main is 0, workers are 1-7)
    DpdkConfig config3;
    config3.core_mask = "0xff";
    PmdThreadConfig pmd3;
    pmd3.lcore_id = 2;  // Use lcore 2 (worker lcore)
    config3.pmd_threads.push_back(pmd3);
    TestCase("Worker lcore available - coremask 0xff with pmd_threads", 
             ConfigValidator::Validate(config3).ok());
    
    // Test with empty pmd_threads (should pass even with single lcore)
    DpdkConfig config4;
    config4.core_mask = "0x01";
    // No pmd_threads added
    TestCase("No pmd_threads - coremask 0x01 without pmd_threads", 
             ConfigValidator::Validate(config4).ok());
    
    // Test with coremask 0x02 (only bit 1, main lcore is 1)
    DpdkConfig config5;
    config5.core_mask = "0x02";
    PmdThreadConfig pmd5;
    pmd5.lcore_id = 2;  // Try to use lcore 2, but it doesn't exist
    config5.pmd_threads.push_back(pmd5);
    TestCase("No worker lcores - coremask 0x02 with pmd_threads", 
             !ConfigValidator::Validate(config5).ok());
    
    std::cout << "--- End of worker lcore availability tests ---\n\n";
  }

  // Test lcore assignment validation
  {
    std::cout << "\n--- Testing lcore assignment validation ---\n";
    
    // Test PMD thread using main lcore (should fail)
    DpdkConfig config1;
    config1.core_mask = "0xff";  // Main lcore is 0
    PmdThreadConfig pmd1;
    pmd1.lcore_id = 0;  // Try to use main lcore
    config1.pmd_threads.push_back(pmd1);
    TestCase("PMD thread cannot use main lcore", 
             !ConfigValidator::Validate(config1).ok());
    
    // Test PMD thread using lcore not in coremask (should fail)
    DpdkConfig config2;
    config2.core_mask = "0x0f";  // Lcores 0-3 available
    PmdThreadConfig pmd2;
    pmd2.lcore_id = 5;  // Lcore 5 not in coremask
    config2.pmd_threads.push_back(pmd2);
    TestCase("PMD thread lcore not in coremask", 
             !ConfigValidator::Validate(config2).ok());
    
    // Test duplicate lcore assignments (should fail)
    DpdkConfig config3;
    config3.core_mask = "0xff";  // Lcores 0-7 available
    PmdThreadConfig pmd3a;
    pmd3a.lcore_id = 1;
    config3.pmd_threads.push_back(pmd3a);
    PmdThreadConfig pmd3b;
    pmd3b.lcore_id = 1;  // Duplicate lcore
    config3.pmd_threads.push_back(pmd3b);
    TestCase("Duplicate lcore assignment", 
             !ConfigValidator::Validate(config3).ok());
    
    // Test valid PMD thread assignments
    DpdkConfig config4;
    config4.core_mask = "0xff";  // Lcores 0-7 available, main is 0
    PmdThreadConfig pmd4a;
    pmd4a.lcore_id = 1;  // Valid worker lcore
    config4.pmd_threads.push_back(pmd4a);
    PmdThreadConfig pmd4b;
    pmd4b.lcore_id = 2;  // Different valid worker lcore
    config4.pmd_threads.push_back(pmd4b);
    TestCase("Valid PMD thread lcore assignments", 
             ConfigValidator::Validate(config4).ok());
    
    // Test with coremask 0x06 (bits 1 and 2, main lcore is 1)
    DpdkConfig config5;
    config5.core_mask = "0x06";  // Lcores 1-2 available, main is 1
    PmdThreadConfig pmd5;
    pmd5.lcore_id = 2;  // Valid worker lcore
    config5.pmd_threads.push_back(pmd5);
    TestCase("Valid PMD thread with non-zero main lcore", 
             ConfigValidator::Validate(config5).ok());
    
    // Test PMD thread trying to use main lcore when main is not 0
    DpdkConfig config6;
    config6.core_mask = "0x06";  // Lcores 1-2 available, main is 1
    PmdThreadConfig pmd6;
    pmd6.lcore_id = 1;  // Try to use main lcore (which is 1)
    config6.pmd_threads.push_back(pmd6);
    TestCase("PMD thread cannot use main lcore (non-zero main)", 
             !ConfigValidator::Validate(config6).ok());
    
    // Test multiple valid PMD threads with various lcores
    DpdkConfig config7;
    config7.core_mask = "0xff";  // Lcores 0-7 available, main is 0
    PmdThreadConfig pmd7a;
    pmd7a.lcore_id = 1;
    config7.pmd_threads.push_back(pmd7a);
    PmdThreadConfig pmd7b;
    pmd7b.lcore_id = 3;
    config7.pmd_threads.push_back(pmd7b);
    PmdThreadConfig pmd7c;
    pmd7c.lcore_id = 7;
    config7.pmd_threads.push_back(pmd7c);
    TestCase("Multiple valid PMD threads with different lcores", 
             ConfigValidator::Validate(config7).ok());
    
    std::cout << "--- End of lcore assignment validation tests ---\n\n";
  }

  // Test RX queue assignment validation
  {
    std::cout << "--- Testing RX queue assignment validation ---\n";
    
    // Test RX queue with unknown port
    DpdkConfig config1;
    config1.core_mask = "0x03";  // Lcores 0-1 available, main is 0
    PmdThreadConfig pmd1;
    pmd1.lcore_id = 1;
    QueueAssignment rx_queue1;
    rx_queue1.port_id = 0;  // Port 0 doesn't exist
    rx_queue1.queue_id = 0;
    pmd1.rx_queues.push_back(rx_queue1);
    config1.pmd_threads.push_back(pmd1);
    TestCase("RX queue with unknown port", 
             !ConfigValidator::Validate(config1).ok());
    
    // Test RX queue with out-of-range queue_id
    DpdkConfig config2;
    config2.core_mask = "0x03";
    DpdkPortConfig port2;
    port2.port_id = 0;
    port2.num_rx_queues = 2;  // Only queues 0-1 are valid
    port2.num_tx_queues = 2;
    port2.num_descriptors = 512;
    port2.mbuf_pool_size = 8192;
    port2.mbuf_size = 2048;
    config2.ports.push_back(port2);
    PmdThreadConfig pmd2;
    pmd2.lcore_id = 1;
    QueueAssignment rx_queue2;
    rx_queue2.port_id = 0;
    rx_queue2.queue_id = 2;  // Out of range (max is 1)
    pmd2.rx_queues.push_back(rx_queue2);
    config2.pmd_threads.push_back(pmd2);
    TestCase("RX queue out of range", 
             !ConfigValidator::Validate(config2).ok());
    
    // Test duplicate RX queue assignment
    DpdkConfig config3;
    config3.core_mask = "0x07";  // Lcores 0-2 available
    DpdkPortConfig port3;
    port3.port_id = 0;
    port3.num_rx_queues = 2;
    port3.num_tx_queues = 2;
    port3.num_descriptors = 512;
    port3.mbuf_pool_size = 8192;
    port3.mbuf_size = 2048;
    config3.ports.push_back(port3);
    PmdThreadConfig pmd3a;
    pmd3a.lcore_id = 1;
    QueueAssignment rx_queue3a;
    rx_queue3a.port_id = 0;
    rx_queue3a.queue_id = 0;
    pmd3a.rx_queues.push_back(rx_queue3a);
    config3.pmd_threads.push_back(pmd3a);
    PmdThreadConfig pmd3b;
    pmd3b.lcore_id = 2;
    QueueAssignment rx_queue3b;
    rx_queue3b.port_id = 0;
    rx_queue3b.queue_id = 0;  // Duplicate of queue in pmd3a
    pmd3b.rx_queues.push_back(rx_queue3b);
    config3.pmd_threads.push_back(pmd3b);
    TestCase("Duplicate RX queue assignment", 
             !ConfigValidator::Validate(config3).ok());
    
    // Test valid RX queue assignments
    DpdkConfig config4;
    config4.core_mask = "0x07";
    DpdkPortConfig port4;
    port4.port_id = 0;
    port4.num_rx_queues = 4;
    port4.num_tx_queues = 4;
    port4.num_descriptors = 512;
    port4.mbuf_pool_size = 8192;
    port4.mbuf_size = 2048;
    config4.ports.push_back(port4);
    PmdThreadConfig pmd4a;
    pmd4a.lcore_id = 1;
    QueueAssignment rx_queue4a;
    rx_queue4a.port_id = 0;
    rx_queue4a.queue_id = 0;
    pmd4a.rx_queues.push_back(rx_queue4a);
    QueueAssignment rx_queue4a2;
    rx_queue4a2.port_id = 0;
    rx_queue4a2.queue_id = 1;
    pmd4a.rx_queues.push_back(rx_queue4a2);
    config4.pmd_threads.push_back(pmd4a);
    PmdThreadConfig pmd4b;
    pmd4b.lcore_id = 2;
    QueueAssignment rx_queue4b;
    rx_queue4b.port_id = 0;
    rx_queue4b.queue_id = 2;  // Different queue
    pmd4b.rx_queues.push_back(rx_queue4b);
    config4.pmd_threads.push_back(pmd4b);
    TestCase("Valid RX queue assignments", 
             ConfigValidator::Validate(config4).ok());
    
    std::cout << "--- End of RX queue assignment validation tests ---\n\n";
  }

  // Test TX queue assignment validation
  {
    std::cout << "--- Testing TX queue assignment validation ---\n";
    
    // Test TX queue with unknown port
    DpdkConfig config1;
    config1.core_mask = "0x03";  // Lcores 0-1 available, main is 0
    PmdThreadConfig pmd1;
    pmd1.lcore_id = 1;
    QueueAssignment tx_queue1;
    tx_queue1.port_id = 0;  // Port 0 doesn't exist
    tx_queue1.queue_id = 0;
    pmd1.tx_queues.push_back(tx_queue1);
    config1.pmd_threads.push_back(pmd1);
    TestCase("TX queue with unknown port", 
             !ConfigValidator::Validate(config1).ok());
    
    // Test TX queue with out-of-range queue_id
    DpdkConfig config2;
    config2.core_mask = "0x03";
    DpdkPortConfig port2;
    port2.port_id = 0;
    port2.num_rx_queues = 2;
    port2.num_tx_queues = 2;  // Only queues 0-1 are valid
    port2.num_descriptors = 512;
    port2.mbuf_pool_size = 8192;
    port2.mbuf_size = 2048;
    config2.ports.push_back(port2);
    PmdThreadConfig pmd2;
    pmd2.lcore_id = 1;
    QueueAssignment tx_queue2;
    tx_queue2.port_id = 0;
    tx_queue2.queue_id = 2;  // Out of range (max is 1)
    pmd2.tx_queues.push_back(tx_queue2);
    config2.pmd_threads.push_back(pmd2);
    TestCase("TX queue out of range", 
             !ConfigValidator::Validate(config2).ok());
    
    // Test duplicate TX queue assignment
    DpdkConfig config3;
    config3.core_mask = "0x07";  // Lcores 0-2 available
    DpdkPortConfig port3;
    port3.port_id = 0;
    port3.num_rx_queues = 2;
    port3.num_tx_queues = 2;
    port3.num_descriptors = 512;
    port3.mbuf_pool_size = 8192;
    port3.mbuf_size = 2048;
    config3.ports.push_back(port3);
    PmdThreadConfig pmd3a;
    pmd3a.lcore_id = 1;
    QueueAssignment tx_queue3a;
    tx_queue3a.port_id = 0;
    tx_queue3a.queue_id = 0;
    pmd3a.tx_queues.push_back(tx_queue3a);
    config3.pmd_threads.push_back(pmd3a);
    PmdThreadConfig pmd3b;
    pmd3b.lcore_id = 2;
    QueueAssignment tx_queue3b;
    tx_queue3b.port_id = 0;
    tx_queue3b.queue_id = 0;  // Duplicate of queue in pmd3a
    pmd3b.tx_queues.push_back(tx_queue3b);
    config3.pmd_threads.push_back(pmd3b);
    TestCase("Duplicate TX queue assignment", 
             !ConfigValidator::Validate(config3).ok());
    
    // Test valid TX queue assignments
    DpdkConfig config4;
    config4.core_mask = "0x07";
    DpdkPortConfig port4;
    port4.port_id = 0;
    port4.num_rx_queues = 4;
    port4.num_tx_queues = 4;
    port4.num_descriptors = 512;
    port4.mbuf_pool_size = 8192;
    port4.mbuf_size = 2048;
    config4.ports.push_back(port4);
    PmdThreadConfig pmd4a;
    pmd4a.lcore_id = 1;
    QueueAssignment tx_queue4a;
    tx_queue4a.port_id = 0;
    tx_queue4a.queue_id = 0;
    pmd4a.tx_queues.push_back(tx_queue4a);
    QueueAssignment tx_queue4a2;
    tx_queue4a2.port_id = 0;
    tx_queue4a2.queue_id = 1;
    pmd4a.tx_queues.push_back(tx_queue4a2);
    config4.pmd_threads.push_back(pmd4a);
    PmdThreadConfig pmd4b;
    pmd4b.lcore_id = 2;
    QueueAssignment tx_queue4b;
    tx_queue4b.port_id = 0;
    tx_queue4b.queue_id = 2;  // Different queue
    pmd4b.tx_queues.push_back(tx_queue4b);
    config4.pmd_threads.push_back(pmd4b);
    TestCase("Valid TX queue assignments", 
             ConfigValidator::Validate(config4).ok());
    
    // Test valid mixed RX and TX queue assignments
    DpdkConfig config5;
    config5.core_mask = "0x07";
    DpdkPortConfig port5;
    port5.port_id = 0;
    port5.num_rx_queues = 4;
    port5.num_tx_queues = 4;
    port5.num_descriptors = 512;
    port5.mbuf_pool_size = 8192;
    port5.mbuf_size = 2048;
    config5.ports.push_back(port5);
    PmdThreadConfig pmd5a;
    pmd5a.lcore_id = 1;
    QueueAssignment rx_queue5a;
    rx_queue5a.port_id = 0;
    rx_queue5a.queue_id = 0;
    pmd5a.rx_queues.push_back(rx_queue5a);
    QueueAssignment tx_queue5a;
    tx_queue5a.port_id = 0;
    tx_queue5a.queue_id = 0;
    pmd5a.tx_queues.push_back(tx_queue5a);
    config5.pmd_threads.push_back(pmd5a);
    PmdThreadConfig pmd5b;
    pmd5b.lcore_id = 2;
    QueueAssignment rx_queue5b;
    rx_queue5b.port_id = 0;
    rx_queue5b.queue_id = 1;
    pmd5b.rx_queues.push_back(rx_queue5b);
    QueueAssignment tx_queue5b;
    tx_queue5b.port_id = 0;
    tx_queue5b.queue_id = 1;
    pmd5b.tx_queues.push_back(tx_queue5b);
    config5.pmd_threads.push_back(pmd5b);
    TestCase("Valid mixed RX and TX queue assignments", 
             ConfigValidator::Validate(config5).ok());
    
    std::cout << "--- End of TX queue assignment validation tests ---\n\n";
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
