# Implementation Plan: End-to-End Test Framework

## Overview

This plan implements a comprehensive pytest-based end-to-end test framework for the DPDK control plane system. The framework uses DPDK's net_tap virtual PMD driver to enable automated testing without physical NICs, supporting CI/CD integration and development workflows.

The implementation creates a test directory structure with Python fixtures for configuration generation, process management, control client interaction, and TAP interface verification. Tests cover process lifecycle, control plane commands, PMD thread configurations, and multi-configuration scenarios.

## Tasks

- [x] 1. Create test directory structure and move existing files
  - Create tests/ directory with subdirectories: e2e/, fixtures/, scripts/, docs/
  - Move manual_test_commands.sh to tests/scripts/
  - Move test_control_plane.sh to tests/scripts/
  - Move VERIFICATION_SUMMARY.md to tests/docs/
  - Move TESTING_GUIDE.md to tests/docs/
  - Create __init__.py files for Python packages
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

- [ ] 2. Implement test configuration generator
  - [x] 2.1 Create config_generator.py with TestConfigGenerator class
    - Implement generate_config() method with net_tap vdev specifications
    - Implement generate_core_mask() for thread-to-core mapping
    - Implement distribute_queues() for round-robin queue assignment
    - Implement write_config() for JSON serialization
    - Support parameters: num_ports (1-2), num_threads (1-2), num_queues (1-2)
    - Include --no-huge, --no-pci, and --vdev flags in additional_params
    - _Requirements: 1.3, 1.4, 1.5, 1.6, 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_
  
  - [ ]* 2.2 Write property test for configuration generation
    - **Property 1: Configuration Generation Completeness**
    - **Validates: Requirements 1.3, 1.4, 1.5, 1.6, 7.1, 7.2, 7.3, 7.4, 7.5, 7.6**
  
  - [ ]* 2.3 Write property test for configuration round-trip
    - **Property 2: Configuration Round-Trip Preservation**
    - **Validates: Requirements 7.7**

- [ ] 3. Implement DPDK process manager
  - [x] 3.1 Create dpdk_process.py with DpdkProcess class
    - Implement start() method using subprocess.Popen with --no-huge flag
    - Implement wait_for_ready() with timeout and "Control plane ready" detection
    - Implement terminate() with graceful (SIGTERM) and forced (SIGKILL) modes
    - Implement output capture in background thread for stdout/stderr
    - Implement is_running(), get_stdout(), get_stderr(), get_exit_code() methods
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.7, 4.8, 9.1_
  
  - [ ]* 3.2 Write unit tests for process manager
    - Test process launch with valid configuration
    - Test initialization timeout handling
    - Test graceful and forced termination
    - Test output capture functionality
    - _Requirements: 4.1, 4.2, 4.5, 4.8_

- [ ] 4. Implement control client
  - [x] 4.1 Create control_client.py with ControlClient class
    - Implement connect() method with retry logic for Unix socket
    - Implement send_command() for JSON command serialization and response parsing
    - Implement convenience methods: status(), get_threads(), shutdown()
    - Implement close() and context manager interface (__enter__, __exit__)
    - Set socket timeout to 5 seconds
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.7_
  
  - [ ]* 4.2 Write unit tests for control client
    - Test socket connection with retry
    - Test command serialization and response parsing
    - Test timeout handling
    - Test error response handling
    - _Requirements: 5.1, 5.2, 5.3, 5.6_

- [ ] 5. Implement TAP interface manager
  - [x] 5.1 Create tap_interface.py with TapInterfaceManager class
    - Implement interface_exists() using 'ip link show' command
    - Implement wait_for_interface() with polling and timeout
    - Implement get_interface_info() to parse interface state and MTU
    - Implement set_interface_up() using 'ip link set up' command
    - Implement verify_interfaces() to check multiple interfaces
    - _Requirements: 3.1, 3.2, 3.3, 3.5, 3.6, 3.7_
  
  - [ ]* 5.2 Write unit tests for TAP interface manager
    - Test interface existence checking
    - Test interface info parsing
    - Test interface state management
    - _Requirements: 3.1, 3.6_

- [ ] 6. Create pytest configuration and fixtures
  - [x] 6.1 Create conftest.py with pytest fixtures
    - Implement binary_path fixture (session scope)
    - Implement test_output_dir fixture using tmp_path_factory
    - Implement test_config fixture with parameterization support
    - Implement dpdk_process fixture with automatic lifecycle management
    - Implement control_client fixture with automatic connection
    - Implement tap_interfaces fixture with verification and cleanup
    - _Requirements: 8.1, 8.2, 8.3, 8.4_
  
  - [x] 6.2 Create pytest.ini configuration file
    - Configure test discovery patterns
    - Add command-line options for verbose output and JUnit XML
    - Define test markers: slow, requires_root, multi_thread, multi_queue
    - Set default timeout to 60 seconds per test
    - _Requirements: 8.5, 8.6, 8.7_

- [ ] 7. Implement process lifecycle tests
  - [x] 7.1 Create test_process_lifecycle.py with TestProcessLifecycle class
    - Implement test_launch_with_net_tap() to verify process starts with net_tap
    - Implement test_initialization_output() to check for expected log messages
    - Implement test_graceful_shutdown() to verify shutdown command works
    - Implement test_startup_timeout() to verify timeout enforcement
    - Implement test_tap_interface_creation() to verify dtap0, dtap1 exist
    - _Requirements: 4.1, 4.2, 4.3, 4.5, 4.7, 3.1, 3.6, 1.1, 1.2_
  
  - [ ]* 7.2 Write property tests for process lifecycle
    - **Property 3: TAP Interface Lifecycle**
    - **Property 4: Process Launch with Required Flags**
    - **Property 5: Process Output Capture**
    - **Property 6: Initialization Sequencing**
    - **Property 7: Graceful Termination**
    - **Property 8: Startup Timeout Enforcement**
    - **Validates: Requirements 3.1, 3.3, 3.5, 3.6, 4.1, 4.2, 4.3, 4.4, 4.5, 4.7, 4.8, 9.1**

- [ ] 8. Implement control plane tests
  - [x] 8.1 Create test_control_plane.py with TestControlPlane class
    - Implement test_status_command() to verify status response format
    - Implement test_get_threads_command() to verify thread info structure
    - Implement test_invalid_command() to verify error handling
    - Implement test_malformed_json() to verify JSON parsing errors
    - Implement test_command_response_timing() to verify performance
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7, 5.8_
  
  - [ ]* 8.2 Write property tests for control plane
    - **Property 9: Control Client Connection**
    - **Property 10: Command Response Validity**
    - **Property 11: Error Command Handling**
    - **Property 12: Command Response Timing**
    - **Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7, 5.8**

- [ ] 9. Implement PMD thread tests
  - [x] 9.1 Create test_pmd_threads.py with TestPmdThreads class
    - Implement test_thread_count() parameterized for 1-2 threads
    - Implement test_queue_distribution() parameterized for 1-2 queues
    - Implement test_lcore_assignments() to verify unique lcores (not 0)
    - Implement test_thread_configuration_verification() to check get_threads response
    - _Requirements: 6.1, 6.2, 6.4, 6.5, 6.6_
  
  - [ ]* 9.2 Write property tests for PMD threads
    - **Property 13: Thread Configuration Verification**
    - **Property 14: Test Parameterization Support**
    - **Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6**

- [ ] 10. Implement multi-configuration tests
  - [x] 10.1 Create test_multi_config.py with TestMultiConfiguration class
    - Implement test_configuration_matrix() parameterized with combinations:
      - (1 port, 1 thread, 1 queue)
      - (1 port, 2 threads, 2 queues)
      - (2 ports, 2 threads, 2 queues)
    - Verify process starts and responds to commands for each configuration
    - _Requirements: 6.1, 6.2, 6.3, 6.4_
  
  - [ ]* 10.2 Write property test for no-hugepage support
    - **Property 15: No-Hugepage Initialization**
    - **Validates: Requirements 9.1, 9.2**

- [x] 11. Checkpoint - Ensure all basic tests pass
  - Run pytest tests/e2e/ to verify all implemented tests pass
  - Verify TAP interfaces are created and cleaned up properly
  - Verify process lifecycle management works correctly
  - Ask the user if questions arise

- [ ] 12. Implement performance and reliability tests
  - [~] 12.1 Create test_performance.py with TestPerformance class
    - Implement test_command_response_time() to measure average/max latency
    - Implement test_rapid_command_sequence() to test command throughput
    - Implement test_concurrent_connections() to test multiple clients
    - _Requirements: 13.1, 13.2, 13.3_
  
  - [ ]* 12.2 Write property tests for reliability
    - **Property 17: Concurrent Connection Handling**
    - **Property 18: Control Operations Non-Interference**
    - **Property 19: Signal Handling Reliability**
    - **Validates: Requirements 13.2, 13.5, 13.6, 13.7**

- [ ] 13. Create test documentation and scripts
  - [~] 13.1 Create tests/README.md
    - Document framework overview and quick start
    - Document directory structure and test categories
    - Document how to run tests (all, specific, with markers)
    - Document fixtures and their usage
    - Document troubleshooting common issues
    - Document CI/CD integration
    - _Requirements: 2.7, 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7, 10.8_
  
  - [~] 13.2 Create tests/scripts/run_all_tests.sh
    - Run Bazel unit tests first
    - Run pytest e2e tests second
    - Exit with appropriate status code
    - _Requirements: 12.3, 12.4, 14.1, 14.2_
  
  - [~] 13.3 Update tests/docs/TESTING_GUIDE.md
    - Add section on e2e test framework
    - Document net_tap driver selection rationale
    - Document VM and no-hugepage support
    - _Requirements: 1.2, 9.3, 9.4, 10.2, 10.8_

- [ ] 14. Implement error handling and diagnostics
  - [~] 14.1 Enhance fixtures with failure diagnostics
    - Capture and save process output on test failure
    - Capture and save configuration on test failure
    - Capture and save control plane responses on test failure
    - Log all test operations for debugging
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.7_
  
  - [ ]* 14.2 Write property tests for diagnostics
    - **Property 16: Test Failure Diagnostics**
    - **Validates: Requirements 11.1, 11.2, 11.3, 11.6, 11.7**

- [ ] 15. Implement CI/CD integration support
  - [~] 15.1 Configure pytest for CI/CD
    - Ensure tests run without interactive input
    - Configure JUnit XML output generation
    - Configure HTML report generation
    - Set reasonable timeout limits (5 minutes for full suite)
    - _Requirements: 14.1, 14.2, 14.3, 14.5_
  
  - [ ]* 15.2 Write property tests for CI/CD requirements
    - **Property 20: Non-Interactive Execution**
    - **Property 21: Exit Code Correctness**
    - **Property 22: Test Suite Timeout**
    - **Property 23: Machine-Readable Results**
    - **Validates: Requirements 14.1, 14.2, 14.3, 14.5**
  
  - [~] 15.3 Create example CI/CD configuration
    - Create .github/workflows/e2e-tests.yml example
    - Document required CI environment setup
    - Document privilege requirements and skip logic
    - _Requirements: 14.4, 14.6, 14.7_

- [~] 16. Final checkpoint - Verify complete test suite
  - Run full test suite: pytest tests/e2e/ -v
  - Verify all test categories pass (lifecycle, control plane, PMD threads, multi-config)
  - Verify JUnit XML and HTML reports are generated
  - Run tests/scripts/run_all_tests.sh to verify integration with unit tests
  - Verify tests work in VM environment without hugepages
  - Ask the user if questions arise

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation at reasonable breaks
- Property tests validate universal correctness properties from the design
- The framework uses Python/pytest as specified in the design document
- Default configuration uses 1-2 threads and 1-2 queues for VM resource constraints
- Tests use net_tap virtual PMD driver for testing without physical NICs
- All tests should work in VM environments without hugepages (--no-huge flag)
