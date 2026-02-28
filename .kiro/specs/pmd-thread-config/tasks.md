# Implementation Plan: PMD Thread Configuration

## Overview

This implementation extends the existing DPDK configuration system to support PMD thread configuration. The work involves adding new data structures to dpdk_config.h, extending the config parser to handle pmd_threads JSON arrays, adding validation logic for lcore and queue assignments, updating the config printer to serialize PMD thread configurations, and implementing a two-class architecture (PMDThread and PMDThreadManager) that mirrors the existing PortManager/DpdkPort pattern for managing thread lifecycle and execution.

## Tasks

- [x] 1. Add PMD thread data structures to config header
  - Add QueueAssignment struct with port_id and queue_id fields
  - Add PmdThreadConfig struct with lcore_id, rx_queues, and tx_queues fields
  - Add pmd_threads vector to DpdkConfig struct
  - _Requirements: 1.1, 1.2, 4.1, 4.2, 4.5_

- [x] 2. Extend config parser to parse PMD thread configuration
  - [x] 2.1 Implement parsing for pmd_threads array in ConfigParser::ParseString
    - Parse optional "pmd_threads" array from JSON
    - Handle empty/missing pmd_threads field gracefully
    - Validate array structure and element types
    - _Requirements: 1.1, 1.2, 1.8_
  
  - [x] 2.2 Implement parsing for individual PMD thread objects
    - Parse required "lcore_id" field as unsigned integer
    - Parse optional "rx_queues" array
    - Parse optional "tx_queues" array
    - Return appropriate error messages for missing/invalid fields
    - _Requirements: 1.3, 1.4, 1.5_
  
  - [x] 2.3 Implement queue assignment parsing helper
    - Parse "port_id" field as unsigned integer
    - Parse "queue_id" field as unsigned integer
    - Validate object structure
    - Return descriptive errors with lcore context
    - _Requirements: 1.6, 1.7_
  
  - [ ]* 2.4 Write unit tests for config parser PMD thread parsing
    - Test valid pmd_threads configurations
    - Test missing/invalid fields
    - Test empty arrays and missing pmd_threads field
    - Test type validation errors
    - _Requirements: 1.1-1.8_

- [x] 3. Implement coremask parsing utilities for validator
  - [x] 3.1 Add ParseCoremask helper function to ConfigValidator
    - Parse hexadecimal coremask string (with/without 0x prefix)
    - Extract bit positions to determine available lcore IDs
    - Support up to 64-bit masks
    - Return set of available lcore IDs
    - _Requirements: 2.2, 7.1_
  
  - [x] 3.2 Add DetermineMainLcore helper function to ConfigValidator
    - Return lowest-numbered lcore from coremask as main lcore
    - Handle empty/missing coremask gracefully
    - _Requirements: 2.1, 7.1, 7.2_
  
  - [x] 3.3 Add FindPort helper function to ConfigValidator
    - Search ports vector for matching port_id
    - Return pointer to port config or nullptr
    - _Requirements: 2.6_
  
  - [ ]* 3.4 Write unit tests for coremask parsing utilities
    - Test various coremask formats (0xff, 0xFF, ff)
    - Test edge cases (empty, single bit, all bits)
    - Test main lcore determination
    - Test port lookup
    - _Requirements: 2.1, 2.2, 7.1_

- [x] 4. Extend config validator to validate PMD thread configuration
  - [x] 4.1 Implement worker lcore availability check
    - Parse coremask to get available lcores
    - Determine main lcore
    - Verify at least one worker lcore exists
    - Return error if only main lcore available
    - _Requirements: 2.1, 2.2, 7.4_
  
  - [x] 4.2 Implement lcore assignment validation
    - Check each PMD thread lcore is not the main lcore
    - Check each PMD thread lcore is in coremask
    - Check for duplicate lcore assignments across PMD threads
    - _Requirements: 2.1, 2.2, 2.3, 7.2_
  
  - [x] 4.3 Implement RX queue assignment validation
    - Verify port_id references a configured port
    - Verify queue_id is within port's num_rx_queues range
    - Check for duplicate (port_id, queue_id) pairs across all PMD threads
    - _Requirements: 2.4, 2.6, 2.7_
  
  - [x] 4.4 Implement TX queue assignment validation
    - Verify port_id references a configured port
    - Verify queue_id is within port's num_tx_queues range
    - Check for duplicate (port_id, queue_id) pairs across all PMD threads
    - _Requirements: 2.5, 2.6, 2.8_
  
  - [ ]* 4.5 Write property test for main lcore exclusion
    - **Property 2: Main Lcore Exclusion**
    - **Validates: Requirements 2.1, 7.1, 7.2**
    - Generate random coremasks and PMD thread configs
    - Verify validation rejects configs using main lcore
  
  - [ ]* 4.6 Write property test for lcore uniqueness
    - **Property 3: Lcore Uniqueness**
    - **Validates: Requirement 2.3**
    - Generate configs with duplicate lcore assignments
    - Verify validation rejects duplicate lcores
  
  - [ ]* 4.7 Write property test for RX queue uniqueness
    - **Property 4: Queue Assignment Uniqueness (RX)**
    - **Validates: Requirement 2.4**
    - Generate configs with duplicate RX queue assignments
    - Verify validation rejects duplicate RX queues
  
  - [ ]* 4.8 Write property test for TX queue uniqueness
    - **Property 5: Queue Assignment Uniqueness (TX)**
    - **Validates: Requirement 2.5**
    - Generate configs with duplicate TX queue assignments
    - Verify validation rejects duplicate TX queues
  
  - [ ]* 4.9 Write property test for port reference validity
    - **Property 6: Port Reference Validity**
    - **Validates: Requirement 2.6**
    - Generate configs with invalid port references
    - Verify validation rejects unknown port IDs
  
  - [ ]* 4.10 Write property test for RX queue range validity
    - **Property 7: Queue Range Validity (RX)**
    - **Validates: Requirement 2.7**
    - Generate configs with out-of-range RX queue IDs
    - Verify validation rejects invalid queue IDs
  
  - [ ]* 4.11 Write property test for TX queue range validity
    - **Property 8: Queue Range Validity (TX)**
    - **Validates: Requirement 2.8**
    - Generate configs with out-of-range TX queue IDs
    - Verify validation rejects invalid queue IDs
  
  - [ ]* 4.12 Write property test for lcore availability
    - **Property 9: Lcore Availability**
    - **Validates: Requirement 2.2**
    - Generate configs with lcores not in coremask
    - Verify validation rejects unavailable lcores

- [x] 5. Extend config printer to serialize PMD thread configuration
  - [x] 5.1 Implement pmd_threads array serialization in ConfigPrinter::ToJson
    - Serialize pmd_threads vector to JSON array
    - Skip serialization if pmd_threads is empty
    - _Requirements: 3.1_
  
  - [x] 5.2 Implement individual PMD thread object serialization
    - Serialize lcore_id field
    - Serialize rx_queues array (skip if empty)
    - Serialize tx_queues array (skip if empty)
    - _Requirements: 3.2, 3.3, 3.4_
  
  - [x] 5.3 Implement queue assignment serialization
    - Serialize port_id and queue_id fields for each queue
    - _Requirements: 3.5_
  
  - [ ]* 5.4 Write property test for parse-print round trip
    - **Property 1: Parse-Print Round Trip Consistency**
    - **Validates: Requirements 1.1-1.8, 3.1-3.6**
    - Generate random valid PMD thread configurations
    - Parse, print, parse again and verify equivalence
  
  - [ ]* 5.5 Write unit tests for config printer PMD thread serialization
    - Test serialization of complete pmd_threads configurations
    - Test empty arrays and missing fields
    - Test output JSON format matches expected schema
    - _Requirements: 3.1-3.5_

- [x] 6. Update known_fields set in config parser
  - Add "pmd_threads" to the known_fields set in ConfigParser::ParseString
  - Prevents pmd_threads from being added to additional_params
  - _Requirements: 1.1_

- [x] 7. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Create example dpdk.json with PMD thread configuration
  - Add example pmd_threads configuration to dpdk.json or create test fixture
  - Include multiple PMD threads with various queue assignments
  - Demonstrate valid configuration format
  - _Requirements: 1.1-1.8_

- [x] 9. Final checkpoint - Verify integration
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Refactor PmdThread from static class to instance-based class
  - [x] 10.1 Refactor PmdThread class to instance-based design
    - Remove all static methods except RunStub(void* arg)
    - Add constructor that takes PmdThreadConfig and stores it as member
    - Add GetLcoreId(), GetRxQueues(), GetTxQueues() accessor methods
    - Rename WorkerMain to Run() and make it a private instance method
    - Keep static RunStub(void* arg) that casts void* to PMDThread* and calls Run()
    - Move ProcessPackets logic into Run() method
    - Preserve existing packet processing loop and stop_flag behavior
    - Keep verbose logging functionality in Run() method
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_
  
  - [ ]* 10.2 Write unit tests for refactored PMDThread class
    - Test constructor stores configuration correctly
    - Test accessors return correct values
    - Test with various queue configurations
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_
  
  - [ ]* 10.3 Write property test for thread configuration preservation
    - **Property 11: Thread Configuration Preservation**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**
    - Generate random PmdThreadConfig instances
    - Verify PMDThread accessors return exact configuration values

- [x] 11. Extract thread management logic into new PMDThreadManager class
  - [x] 11.1 Create PMDThreadManager class with extracted logic
    - Create config/pmd_thread_manager.h with class declaration
    - Create config/pmd_thread_manager.cc with implementation
    - Extract LaunchThreads logic from PmdThread::LaunchThreads
    - Extract WaitForThreads logic from PmdThread::WaitForThreads
    - Extract StopAllThreads logic from PmdThread::StopAllThreads
    - Move static stop_flag_ from PmdThread to PMDThreadManager
    - Add GetThread(lcore_id), GetLcoreIds(), GetThreadCount() accessor methods
    - Store threads in unordered_map<uint32_t, unique_ptr<PMDThread>>
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_
  
  - [x] 11.2 Refactor thread launch logic to use PMDThread instances
    - Create PMDThread instances from configs (not just pass pointers)
    - Call rte_eal_remote_launch with PMDThread::RunStub and PMDThread* arg
    - Preserve main lcore skipping logic and verbose logging
    - Preserve error handling for launch failures
    - Store created PMDThread instances in unordered_map by lcore_id
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [x] 11.3 Refactor thread wait logic to iterate over stored threads
    - Call rte_eal_wait_lcore for each lcore in the map
    - Preserve error collection and reporting behavior
    - _Requirements: 5.5_
  
  - [x] 11.4 Update existing callers to use PMDThreadManager
    - Find all calls to PmdThread::LaunchThreads (likely in main.cc or dpdk_initializer.cc)
    - Replace with PMDThreadManager instance and method calls
    - Update StopAllThreads and WaitForThreads calls similarly
    - Ensure no breaking changes to external API
    - _Requirements: 5.1, 5.5_
  
  - [ ]* 11.5 Write unit tests for PMDThreadManager class
    - Test LaunchThreads creates correct number of threads
    - Test GetThread returns correct thread by lcore_id
    - Test GetLcoreIds returns all launched lcores
    - Test GetThreadCount returns correct count
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_
  
  - [ ]* 11.6 Write property test for thread-config correspondence
    - **Property 10: Thread-Config Correspondence**
    - **Validates: Requirement 5.1**
    - Generate random valid thread configurations
    - Verify manager creates exactly one thread per config
    - Verify all threads are accessible via GetThread
  
  - [ ]* 11.7 Write integration test for thread launch and wait
    - **Property 12: Launch-Wait Correspondence**
    - **Validates: Requirement 5.5**
    - Launch threads with DPDK EAL initialized
    - Verify WaitForThreads completes successfully
    - Verify rte_eal_wait_lcore called for each lcore
    - Requires multi-core test environment

- [x] 12. Update build system for PMDThreadManager
  - Add pmd_thread_manager.cc to BUILD file (pmd_thread.cc already exists)
  - Ensure DPDK EAL dependencies are included for both files
  - Create test target for pmd_thread_manager_test (pmd_thread_test may already exist)
  - _Requirements: 4.1-4.5, 5.1-5.5_

- [x] 13. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Property tests validate universal correctness properties from the design document (12 total properties)
- Unit tests validate specific examples and edge cases
- The implementation extends existing components without breaking changes
- Coremask parsing utilities are essential for validation logic
- Error messages should be descriptive and include context (lcore_id, port_id, queue_id)
- Tasks 10-11 are REFACTORING tasks: converting existing static PmdThread class to instance-based design and extracting management logic into PMDThreadManager
- The refactoring preserves all existing functionality: stop_flag, verbose logging, main lcore skipping, packet processing loop
- PMDThreadManager mirrors the existing PortManager/DpdkPort architecture pattern
- Main lcore is reserved for control plane and automatically skipped during thread launch
- Integration tests for thread launch/wait require DPDK EAL initialization and multi-core environment
