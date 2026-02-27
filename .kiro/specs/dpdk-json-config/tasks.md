# Implementation Plan: DPDK JSON Configuration

## Overview

This plan implements JSON-based configuration file support for a DPDK C++ application. The implementation follows a modular architecture with separate components for parsing, validation, printing, and DPDK initialization. The feature integrates with the existing Abseil flags system and maintains backward compatibility.

## Tasks

- [x] 1. Set up project structure and dependencies
  - Create config/ directory for new components
  - Add nlohmann_json dependency to MODULE.bazel
  - Update BUILD file with new library targets
  - _Requirements: 2.1, 3.1_

- [x] 2. Implement configuration data structures
  - [x] 2.1 Create dpdk_config.h with DpdkConfig struct
    - Define struct with optional fields for all EAL parameters
    - Include core_mask, memory_channels, pci_allowlist, pci_blocklist, log_level, huge_pages
    - Add additional_params vector for extensibility
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

- [x] 3. Implement JSON parsing
  - [x] 3.1 Create config_parser.h interface
    - Define ConfigParser class with ParseFile and ParseString static methods
    - Use absl::StatusOr<DpdkConfig> return type
    - _Requirements: 2.1, 2.2_
  
  - [x] 3.2 Implement config_parser.cc
    - Implement ParseFile to read file and delegate to ParseString
    - Implement ParseString using nlohmann/json library
    - Handle file not found errors with descriptive messages
    - Handle JSON syntax errors with line numbers
    - Handle empty file errors
    - Parse all supported fields into DpdkConfig struct
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_
  
  - [ ]* 3.3 Write unit tests for config_parser
    - Test valid JSON parsing for all field types
    - Test file not found error handling
    - Test invalid JSON syntax error handling
    - Test empty file error handling
    - _Requirements: 2.3, 2.4, 2.5_

- [x] 4. Implement configuration validation
  - [x] 4.1 Create config_validator.h interface
    - Define ConfigValidator class with Validate static method
    - Define private helper methods for specific validations
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_
  
  - [x] 4.2 Implement config_validator.cc
    - Implement IsValidHexString helper (check 0-9, a-f, A-F, optional 0x prefix)
    - Implement IsValidPciAddress helper (regex: DDDD:BB:DD.F format)
    - Implement IsValidLogLevel helper (range 0-8)
    - Implement Validate method checking all fields
    - Check for PCI address conflicts between allowlist and blocklist
    - Return descriptive error messages for each validation failure
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_
  
  - [ ]* 4.3 Write property test for validation
    - **Property 1: Valid configurations always pass validation**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**
    - Generate random valid configurations and verify Validate returns OK
  
  - [ ]* 4.4 Write unit tests for config_validator
    - Test invalid core_mask formats
    - Test invalid memory_channels values (zero, negative)
    - Test invalid PCI address formats
    - Test invalid log_level values (negative, > 8)
    - Test PCI address conflicts between allowlist and blocklist
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

- [x] 5. Implement configuration printer
  - [x] 5.1 Create config_printer.h interface
    - Define ConfigPrinter class with ToJson static method
    - _Requirements: 6.1, 6.2, 6.3_
  
  - [x] 5.2 Implement config_printer.cc
    - Implement ToJson using nlohmann/json library
    - Serialize all DpdkConfig fields to JSON
    - Format output with configurable indentation (default 2 spaces)
    - Preserve field names and values exactly
    - _Requirements: 6.1, 6.2, 6.3_
  
  - [ ]* 5.3 Write property test for round-trip consistency
    - **Property 2: Parse → Print → Parse preserves configuration**
    - **Validates: Requirements 6.4**
    - Generate random valid configurations, serialize to JSON, parse back, verify equality

- [x] 6. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement DPDK initializer
  - [x] 7.1 Create dpdk_initializer.h interface
    - Define DpdkInitializer class with Initialize and BuildArguments static methods
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7_
  
  - [x] 7.2 Implement dpdk_initializer.cc BuildArguments method
    - Construct argv vector starting with program name
    - Map core_mask to -c argument
    - Map memory_channels to -n argument
    - Map pci_allowlist entries to -a arguments
    - Map pci_blocklist entries to -b arguments
    - Map log_level to --log-level argument
    - Return vector of argument strings
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_
  
  - [x] 7.3 Implement dpdk_initializer.cc Initialize method
    - Call BuildArguments to construct argv
    - Print arguments if verbose flag is true
    - Convert std::vector<std::string> to C-style argc/argv
    - Call rte_eal_init with constructed arguments
    - Handle rte_eal_init errors with descriptive messages
    - Print success message if verbose flag is true
    - _Requirements: 5.1, 5.7, 8.2, 8.3_
  
  - [ ]* 7.4 Write unit tests for dpdk_initializer
    - Test BuildArguments with various configurations
    - Verify correct argument ordering and formatting
    - Test all EAL parameter mappings
    - _Requirements: 5.2, 5.3, 5.4, 5.5, 5.6_

- [x] 8. Integrate with main.cc
  - [x] 8.1 Add -i flag definition to main.cc
    - Define ABSL_FLAG for config file path
    - _Requirements: 1.1, 1.2_
  
  - [x] 8.2 Implement configuration loading logic in main.cc
    - Check if -i flag is provided
    - If provided, call ConfigParser::ParseFile
    - Handle parsing errors with stderr output and non-zero exit
    - Call ConfigValidator::Validate on parsed config
    - Handle validation errors with stderr output and non-zero exit
    - If verbose flag is set, print loaded configuration using ConfigPrinter
    - Call DpdkInitializer::Initialize with parsed config
    - Handle initialization errors with stderr output and non-zero exit
    - If -i flag not provided, maintain existing rte_eal_init behavior
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 2.1, 2.2, 4.6, 5.7, 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 8.1, 8.2, 8.3_

- [x] 9. Update Bazel BUILD file
  - [x] 9.1 Create cc_library targets for new components
    - Add dpdk_config library (header-only)
    - Add config_parser library with nlohmann_json dependency
    - Add config_validator library
    - Add config_printer library with nlohmann_json dependency
    - Add dpdk_initializer library with DPDK dependency
    - Update main binary to depend on new libraries
    - _Requirements: 2.1, 2.2, 4.1, 5.1, 6.1_

- [x] 10. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 11. Update dpdk_config.h with port configuration structures
  - [x] 11.1 Add DpdkPortConfig struct to dpdk_config.h
    - Add fields: port_id, num_rx_queues, num_tx_queues, num_descriptors, mbuf_pool_size, mbuf_size
    - Add documentation comments for each field
    - _Requirements: 3.1_
  
  - [x] 11.2 Add ports vector to DpdkConfig struct
    - Add std::vector<DpdkPortConfig> ports field
    - _Requirements: 3.1_

- [ ] 12. Implement DPDK port class
  - [x] 12.1 Create dpdk_port.h interface
    - Define DpdkPort class with constructor taking DpdkPortConfig
    - Define Initialize(), Start(), Stop() public methods
    - Define GetPortId(), IsInitialized(), IsStarted() accessor methods
    - Define private helper methods: CreateMbufPool(), ConfigurePort(), SetupRxQueues(), SetupTxQueues(), IsPowerOfTwo()
    - _Requirements: 3.1_
  
  - [x] 12.2 Implement dpdk_port.cc
    - Implement constructor and destructor
    - Implement Initialize() method calling helpers in sequence
    - Implement CreateMbufPool() with per-core cache support (cache_size=256)
    - Implement ConfigurePort() with jumbo frame support based on mbuf_size
    - Implement SetupRxQueues() iterating over num_rx_queues
    - Implement SetupTxQueues() iterating over num_tx_queues
    - Implement Start() calling rte_eth_dev_start()
    - Implement Stop() calling rte_eth_dev_stop()
    - Implement IsPowerOfTwo() helper for descriptor validation
    - _Requirements: 3.1_
  
  - [ ]* 12.3 Write unit tests for dpdk_port
    - Test port initialization sequence
    - Test mbuf pool creation
    - Test queue setup
    - Test error handling for invalid configurations

- [ ] 13. Implement port manager class
  - [x] 13.1 Create port_manager.h interface
    - Define PortManager class
    - Define InitializePorts(), StartAllPorts(), StopAllPorts() methods
    - Define GetPort(), GetPortIds(), GetPortCount() accessor methods
    - _Requirements: 3.1_
  
  - [x] 13.2 Implement port_manager.cc
    - Implement InitializePorts() creating DpdkPort instances for each config
    - Implement StartAllPorts() calling Start() on all ports
    - Implement StopAllPorts() calling Stop() on all ports
    - Implement accessor methods
    - Store ports in unordered_map keyed by port_id
    - _Requirements: 3.1_
  
  - [ ]* 13.3 Write unit tests for port_manager
    - Test multiple port initialization
    - Test port lookup by ID
    - Test start/stop all ports

- [ ] 14. Update config_parser.cc for port configurations
  - [x] 14.1 Add port array parsing to ParseString method
    - Parse "ports" JSON array if present
    - For each port object, parse all required fields: port_id, num_rx_queues, num_tx_queues, num_descriptors, mbuf_pool_size, mbuf_size
    - Return error with port_id context if required field is missing
    - Add parsed DpdkPortConfig to config.ports vector
    - _Requirements: 2.2, 2.6, 3.1_

- [ ] 15. Update config_validator.cc for port validation
  - [x] 15.1 Add port configuration validation to Validate method
    - Check for duplicate port_id values across all ports
    - Validate num_rx_queues > 0 for each port
    - Validate num_tx_queues > 0 for each port
    - Validate num_descriptors is power of 2 for each port
    - Validate mbuf_pool_size > 0 for each port
    - Validate mbuf_size > 0 for each port
    - Add warning (not error) if mbuf_pool_size is below recommended minimum (accounting for per-core caches)
    - Return descriptive errors with port_id context
    - _Requirements: 4.1_
  
  - [ ]* 15.2 Write property test for port validation
    - **Property 6: Port ID Uniqueness Validation**
    - **Validates: Port configuration validation requirements**
    - Generate configurations with duplicate port IDs and verify validation fails
  
  - [ ]* 15.3 Write property test for descriptor validation
    - **Property 7: Power of Two Descriptor Validation**
    - **Validates: Port configuration validation requirements**
    - Generate configurations with non-power-of-2 descriptors and verify validation fails

- [ ] 16. Update config_printer.cc for port serialization
  - [x] 16.1 Add port array serialization to ToJson method
    - Serialize config.ports vector to JSON "ports" array
    - For each port, serialize all fields: port_id, num_rx_queues, num_tx_queues, num_descriptors, mbuf_pool_size, mbuf_size
    - Preserve field order and formatting
    - _Requirements: 6.1, 6.2, 6.3_

- [ ] 17. Update dpdk_initializer.cc for port initialization
  - [x] 17.1 Add port initialization to Initialize method
    - After successful rte_eal_init(), check if config.ports is non-empty
    - Create PortManager instance
    - Call InitializePorts() with config.ports
    - Call StartAllPorts()
    - Handle errors with descriptive messages including port_id
    - Print success message if verbose flag is true
    - _Requirements: 5.1, 8.2_

- [ ] 18. Update BUILD file with new libraries
  - [x] 18.1 Add dpdk_port library target
    - Create cc_library for dpdk_port with DPDK dependencies
    - Include dpdk_port.h and dpdk_port.cc
    - _Requirements: 2.1_
  
  - [x] 18.2 Add port_manager library target
    - Create cc_library for port_manager
    - Add dependency on dpdk_port library
    - Include port_manager.h and port_manager.cc
    - _Requirements: 2.1_
  
  - [x] 18.3 Update dpdk_initializer library dependencies
    - Add port_manager to dpdk_initializer dependencies
    - _Requirements: 2.1_

- [x] 19. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- The design uses C++ with Abseil, nlohmann/json, and DPDK libraries
- Property tests validate universal correctness properties (round-trip, validation)
- Unit tests validate specific examples and edge cases
- Integration maintains backward compatibility with existing DPDK initialization
- Tasks 1-10 are already completed
- Tasks 11-19 add port configuration and initialization support
- Task 11 is marked complete as dpdk_config.h has already been updated with port structures
