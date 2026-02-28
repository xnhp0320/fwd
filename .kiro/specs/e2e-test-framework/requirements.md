# Requirements Document: End-to-End Test Framework

## Introduction

This document specifies requirements for an end-to-end test framework for the DPDK control plane system. The framework enables automated testing of the complete system without requiring physical DPDK-compatible NICs by using DPDK virtual PMD drivers. It addresses the missing Task 11 tests from the asio-control-loop spec and provides a comprehensive testing infrastructure for continuous integration and development workflows.

The framework will support testing the main process launch, control plane functionality, PMD thread management, and packet processing using virtual interfaces in VM environments without hugepages.

## Glossary

- **Test_Framework**: The pytest-based test automation system that orchestrates end-to-end tests
- **Virtual_PMD**: The net_tap DPDK Poll Mode Driver used for testing with actual Linux TAP interfaces that support real packet I/O
- **Test_Runner**: The component that executes test cases and collects results
- **Virtual_Interface_Manager**: Component that creates and manages TAP/virtual network interfaces for testing
- **DPDK_Process**: The main DPDK application binary under test
- **Control_Client**: Test component that connects to the Unix socket and sends commands
- **Test_Configuration_Generator**: Component that generates dpdk.json configurations for test scenarios
- **Test_Directory_Structure**: Organized directory hierarchy for test scripts, fixtures, and documentation

## Requirements

### Requirement 1: Virtual PMD Driver Configuration

**User Story:** As a developer, I want to test DPDK functionality without physical NICs, so that I can run tests in CI/CD environments and VMs.

#### Acceptance Criteria

1. THE Test_Framework SHALL use net_tap as the virtual PMD driver for all tests
2. THE Test_Framework SHALL document that net_tap was selected because it creates actual Linux TAP interfaces with multi-queue support, enabling real packet I/O for testing PMD threads
3. WHEN configuring DPDK for testing, THE Test_Configuration_Generator SHALL generate configurations using net_tap virtual PMD
4. THE Test_Configuration_Generator SHALL support configurations with multiple queues per port
5. THE Test_Configuration_Generator SHALL support configurations with multiple PMD threads
6. WHERE net_tap requires specific EAL arguments, THE Test_Configuration_Generator SHALL specify them in the additional_params field of DpdkConfig (--no-huge, --no-pci, --vdev=net_tap0, --vdev=net_tap1, etc.)

### Requirement 2: Test Directory Structure Organization

**User Story:** As a developer, I want organized test files and documentation, so that I can easily find and maintain test resources.

#### Acceptance Criteria

1. THE Test_Framework SHALL create a tests/ directory at project root
2. THE Test_Framework SHALL organize test files into subdirectories: e2e/, fixtures/, scripts/, and docs/
3. THE Test_Framework SHALL move manual_test_commands.sh to tests/scripts/
4. THE Test_Framework SHALL move test_control_plane.sh to tests/scripts/
5. THE Test_Framework SHALL move VERIFICATION_SUMMARY.md to tests/docs/
6. THE Test_Framework SHALL move TESTING_GUIDE.md to tests/docs/
7. THE Test_Framework SHALL create a tests/README.md documenting the directory structure

### Requirement 3: Virtual Interface Management

**User Story:** As a developer, I want automated creation of virtual network interfaces, so that tests can run without manual setup.

#### Acceptance Criteria

1. WHEN DPDK initializes with net_tap vdevs, THE Virtual_Interface_Manager SHALL verify that TAP interfaces (dtap0, dtap1, etc.) are automatically created
2. THE Virtual_Interface_Manager SHALL configure created interfaces with appropriate parameters (MTU, state)
3. WHEN a test completes, THE Virtual_Interface_Manager SHALL clean up all created virtual interfaces
4. IF interface creation fails, THEN THE Virtual_Interface_Manager SHALL return a descriptive error
5. THE Virtual_Interface_Manager SHALL support creating multiple virtual interfaces for multi-port tests
6. THE Virtual_Interface_Manager SHALL verify interface creation succeeded before proceeding with tests
7. THE Virtual_Interface_Manager SHALL document that TAP interfaces can be tested with standard Linux tools (ping, tcpdump, ip commands)

### Requirement 4: DPDK Process Lifecycle Management

**User Story:** As a developer, I want automated DPDK process management, so that tests can launch and control the application under test.

#### Acceptance Criteria

1. WHEN a test begins, THE Test_Runner SHALL launch the DPDK_Process with --no-huge flag
2. THE Test_Runner SHALL pass generated test configuration to DPDK_Process
3. THE Test_Runner SHALL capture stdout and stderr from DPDK_Process for test validation
4. THE Test_Runner SHALL wait for DPDK_Process initialization to complete before running test assertions
5. WHEN a test completes, THE Test_Runner SHALL terminate DPDK_Process gracefully via shutdown command
6. IF DPDK_Process fails to start, THEN THE Test_Runner SHALL capture error output and fail the test with diagnostic information
7. THE Test_Runner SHALL enforce a startup timeout (default 30 seconds) and fail tests if exceeded
8. WHEN DPDK_Process does not terminate gracefully, THE Test_Runner SHALL force termination after timeout (default 10 seconds)

### Requirement 5: Control Plane Command Testing

**User Story:** As a developer, I want automated testing of control plane commands, so that I can verify the Unix socket interface works correctly.

#### Acceptance Criteria

1. WHEN DPDK_Process is running, THE Control_Client SHALL connect to the Unix socket
2. THE Control_Client SHALL send JSON commands to the Unix socket
3. THE Control_Client SHALL receive and parse JSON responses from the Unix socket
4. THE Control_Client SHALL validate response structure matches the command protocol specification
5. THE Control_Client SHALL support testing all defined commands (status, get_threads, shutdown)
6. THE Control_Client SHALL test error conditions (invalid JSON, missing fields, unknown commands)
7. THE Control_Client SHALL verify response timing meets performance requirements
8. FOR ALL valid commands, sending the command SHALL produce a success response with expected result structure

### Requirement 6: Multi-Queue and Multi-Thread Testing

**User Story:** As a developer, I want to test configurations with multiple queues and threads, so that I can verify the system works with realistic workloads.

#### Acceptance Criteria

1. THE Test_Framework SHALL support test scenarios with 1 and 2 PMD threads (limited for VM resource constraints)
2. THE Test_Framework SHALL support test scenarios with 1 and 2 queues per port (limited for VM resource constraints)
3. THE Test_Framework SHALL support test scenarios with 1 and 2 virtual ports
4. WHEN testing multi-thread configurations, THE Test_Runner SHALL verify all PMD threads start successfully
5. WHEN testing multi-queue configurations, THE Test_Runner SHALL verify all queues are assigned to threads
6. THE Test_Framework SHALL validate get_threads command returns correct thread count and lcore assignments

### Requirement 7: Test Configuration Generation

**User Story:** As a developer, I want automated generation of test configurations, so that I can easily test different scenarios without manual JSON editing.

#### Acceptance Criteria

1. THE Test_Configuration_Generator SHALL generate valid dpdk.json files for test scenarios
2. THE Test_Configuration_Generator SHALL support parameterized generation (num_ports, num_threads, num_queues)
3. THE Test_Configuration_Generator SHALL use appropriate core_mask for the number of threads
4. THE Test_Configuration_Generator SHALL distribute queues evenly across PMD threads
5. THE Test_Configuration_Generator SHALL include virtual PMD device specifications
6. THE Test_Configuration_Generator SHALL validate generated configurations against the config schema
7. FOR ALL generated configurations, parsing then printing then parsing SHALL produce an equivalent configuration object (round-trip property)

### Requirement 8: Test Framework Implementation

**User Story:** As a developer, I want a pytest-based test framework, so that I can integrate with standard Python testing tools and CI/CD pipelines.

#### Acceptance Criteria

1. THE Test_Framework SHALL use pytest as the test runner
2. THE Test_Framework SHALL provide pytest fixtures for DPDK process management
3. THE Test_Framework SHALL provide pytest fixtures for virtual interface management
4. THE Test_Framework SHALL provide pytest fixtures for control client connections
5. THE Test_Framework SHALL support pytest markers for test categorization (unit, integration, e2e)
6. THE Test_Framework SHALL generate test reports in standard formats (JUnit XML, HTML)
7. THE Test_Framework SHALL support parallel test execution where tests are independent
8. THE Test_Framework SHALL provide clear test output with pass/fail status and diagnostic information

### Requirement 9: VM and No-Hugepage Support

**User Story:** As a developer, I want to run tests in VM environments without hugepages, so that I can test in resource-constrained environments and CI containers.

#### Acceptance Criteria

1. WHEN launching DPDK_Process for tests, THE Test_Runner SHALL include --no-huge flag
2. THE Test_Runner SHALL verify DPDK_Process can initialize without hugepages
3. THE Test_Runner SHALL document memory requirements for no-hugepage mode
4. IF DPDK_Process requires additional flags for VM environments, THEN THE Test_Configuration_Generator SHALL include them
5. THE Test_Framework SHALL validate tests pass in both VM and bare-metal environments

### Requirement 10: Test Documentation and Examples

**User Story:** As a developer, I want comprehensive test documentation, so that I can understand how to run tests and add new test cases.

#### Acceptance Criteria

1. THE Test_Framework SHALL provide a tests/README.md with setup instructions
2. THE Test_Framework SHALL document virtual PMD driver selection rationale (net_tap chosen for real packet I/O and multi-queue support)
3. THE Test_Framework SHALL provide examples of running individual tests
4. THE Test_Framework SHALL provide examples of running test suites
5. THE Test_Framework SHALL document test fixtures and their usage
6. THE Test_Framework SHALL document how to add new test cases
7. THE Test_Framework SHALL document CI/CD integration steps
8. THE Test_Framework SHALL document that net_tap requires kernel support for multi-queue TAP devices

### Requirement 11: Error Handling and Diagnostics

**User Story:** As a developer, I want detailed error diagnostics when tests fail, so that I can quickly identify and fix issues.

#### Acceptance Criteria

1. WHEN a test fails, THE Test_Runner SHALL capture DPDK_Process output
2. WHEN a test fails, THE Test_Runner SHALL capture control plane command responses
3. WHEN a test fails, THE Test_Runner SHALL include configuration details in the failure report
4. IF DPDK_Process crashes, THEN THE Test_Runner SHALL capture the exit code and signal information
5. IF virtual interface creation fails, THEN THE Test_Runner SHALL include system error details
6. THE Test_Framework SHALL log all test operations for debugging
7. THE Test_Framework SHALL preserve test artifacts (logs, configs) on failure

### Requirement 12: Integration with Existing Tests

**User Story:** As a developer, I want the new test framework to complement existing unit tests, so that I have comprehensive test coverage at all levels.

#### Acceptance Criteria

1. THE Test_Framework SHALL run independently from Bazel unit tests
2. THE Test_Framework SHALL document the relationship between unit tests and e2e tests
3. THE Test_Framework SHALL provide a command to run all tests (unit + e2e)
4. THE Test_Framework SHALL support running e2e tests after successful unit test execution
5. THE Test_Framework SHALL not duplicate test coverage provided by existing unit tests

### Requirement 13: Performance and Reliability Testing

**User Story:** As a developer, I want to test system performance and reliability, so that I can ensure the control plane meets operational requirements.

#### Acceptance Criteria

1. THE Test_Framework SHALL measure control plane command response times
2. THE Test_Framework SHALL test multiple concurrent socket connections
3. THE Test_Framework SHALL test rapid command sequences
4. THE Test_Framework SHALL test graceful shutdown under load
5. THE Test_Framework SHALL verify PMD threads continue processing during control operations
6. THE Test_Framework SHALL test signal handling (SIGINT, SIGTERM) reliability
7. THE Test_Framework SHALL validate socket cleanup after shutdown

### Requirement 14: Continuous Integration Support

**User Story:** As a developer, I want tests to run in CI/CD pipelines, so that I can catch regressions automatically.

#### Acceptance Criteria

1. THE Test_Framework SHALL run without interactive input
2. THE Test_Framework SHALL exit with appropriate status codes (0 for success, non-zero for failure)
3. THE Test_Framework SHALL complete within reasonable time limits (default 5 minutes for full suite)
4. THE Test_Framework SHALL support running in containerized environments
5. THE Test_Framework SHALL provide machine-readable test results (JUnit XML)
6. THE Test_Framework SHALL document required CI environment setup (privileges, dependencies)
7. WHERE CI environment lacks privileges, THE Test_Framework SHALL skip tests requiring elevated permissions with clear messages
