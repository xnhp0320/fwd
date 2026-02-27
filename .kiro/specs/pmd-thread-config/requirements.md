# Requirements Document

## Introduction

This feature adds PMD (Poll Mode Driver) thread configuration to the existing DPDK-based packet processing system. The system uses a coremask to specify which CPU cores are available. The first core in the coremask (main lcore) is reserved for control plane operations (boost.asio unix-socket listener), while remaining cores run PMD worker threads that poll network queues for packet processing. Each PMD thread needs explicit configuration specifying which port and queue pairs it should handle for both receive (Rx) and transmit (Tx) operations.

## Glossary

- **PMD_Thread**: A Poll Mode Driver thread that continuously polls network device queues for packets without blocking
- **Main_Lcore**: The first CPU core in the DPDK coremask, reserved for control plane operations
- **Worker_Lcore**: Any CPU core in the coremask other than the main lcore, used for PMD threads
- **Control_Plane**: The main thread running boost.asio to handle unix-socket control requests
- **Data_Plane**: PMD threads that process network packets
- **Queue_Assignment**: A mapping of (port_id, queue_id) pairs to a specific PMD thread
- **Config_Parser**: The component that parses JSON configuration files
- **Config_Printer**: The component that serializes configuration to JSON format
- **PMD_Context**: A data structure holding all queue assignments for a single PMD thread
- **Coremask**: A hexadecimal bitmask specifying which CPU cores DPDK can use

## Requirements

### Requirement 1: PMD Thread Configuration Schema

**User Story:** As a system administrator, I want to configure PMD thread assignments in the JSON file, so that I can control which threads handle which network queues.

#### Acceptance Criteria

1. THE Config_Parser SHALL parse a "pmd_threads" array from the JSON configuration
2. WHEN a "pmd_threads" array is present, THE Config_Parser SHALL parse each element as a PMD thread configuration object
3. FOR each PMD thread configuration, THE Config_Parser SHALL parse a "lcore_id" field as an unsigned integer
4. FOR each PMD thread configuration, THE Config_Parser SHALL parse an "rx_queues" array containing port-queue assignments
5. FOR each PMD thread configuration, THE Config_Parser SHALL parse a "tx_queues" array containing port-queue assignments
6. FOR each queue assignment in "rx_queues" or "tx_queues", THE Config_Parser SHALL parse a "port_id" field as an unsigned integer
7. FOR each queue assignment in "rx_queues" or "tx_queues", THE Config_Parser SHALL parse a "queue_id" field as an unsigned integer
8. WHERE the "pmd_threads" field is absent, THE Config_Parser SHALL create an empty PMD thread configuration list

### Requirement 2: Configuration Validation

**User Story:** As a system administrator, I want the system to validate PMD thread configuration, so that I can catch configuration errors before runtime.

#### Acceptance Criteria

1. WHEN an lcore_id in "pmd_threads" equals the main lcore, THE Config_Validator SHALL return an error indicating the main lcore is reserved
2. WHEN an lcore_id in "pmd_threads" is not present in the coremask, THE Config_Validator SHALL return an error indicating the lcore is not available
3. WHEN the same lcore_id appears in multiple PMD thread configurations, THE Config_Validator SHALL return an error indicating duplicate lcore assignment
4. WHEN the same (port_id, queue_id) pair appears in multiple PMD threads' rx_queues, THE Config_Validator SHALL return an error indicating duplicate queue assignment
5. WHEN the same (port_id, queue_id) pair appears in multiple PMD threads' tx_queues, THE Config_Validator SHALL return an error indicating duplicate queue assignment
6. WHEN a port_id in a queue assignment does not match any configured port, THE Config_Validator SHALL return an error indicating unknown port
7. WHEN an rx queue_id exceeds the num_rx_queues for its port, THE Config_Validator SHALL return an error indicating queue out of range
8. WHEN a tx queue_id exceeds the num_tx_queues for its port, THE Config_Validator SHALL return an error indicating queue out of range

### Requirement 3: Configuration Serialization

**User Story:** As a developer, I want to serialize PMD thread configuration back to JSON, so that I can verify configuration round-trips correctly.

#### Acceptance Criteria

1. THE Config_Printer SHALL serialize the "pmd_threads" array to JSON format
2. FOR each PMD thread configuration, THE Config_Printer SHALL serialize the "lcore_id" field as an unsigned integer
3. FOR each PMD thread configuration, THE Config_Printer SHALL serialize the "rx_queues" array with all port-queue assignments
4. FOR each PMD thread configuration, THE Config_Printer SHALL serialize the "tx_queues" array with all port-queue assignments
5. FOR each queue assignment, THE Config_Printer SHALL serialize both "port_id" and "queue_id" fields as unsigned integers
6. WHEN parsing then printing a valid configuration, THE Config_Parser SHALL produce an equivalent configuration object (round-trip property)

### Requirement 4: PMD Context Data Structure

**User Story:** As a developer, I want a context class for PMD threads, so that each thread knows which queues to poll.

#### Acceptance Criteria

1. THE PMD_Context SHALL store a list of (port_id, rx_queue_id) pairs for receive operations
2. THE PMD_Context SHALL store a list of (port_id, tx_queue_id) pairs for transmit operations
3. THE PMD_Context SHALL provide a method to retrieve all rx queue assignments
4. THE PMD_Context SHALL provide a method to retrieve all tx queue assignments
5. THE PMD_Context SHALL store the lcore_id on which the PMD thread runs

### Requirement 5: PMD Thread Lifecycle Management

**User Story:** As a developer, I want to launch PMD threads on worker lcores, so that packet processing can begin.

#### Acceptance Criteria

1. THE PMD_Thread_Manager SHALL launch one PMD thread per configured lcore_id
2. WHEN launching a PMD thread, THE PMD_Thread_Manager SHALL use rte_eal_remote_launch to execute on the specified lcore
3. WHEN launching a PMD thread, THE PMD_Thread_Manager SHALL pass the corresponding PMD_Context to the thread
4. THE PMD_Thread_Manager SHALL NOT launch a thread on the main lcore
5. THE PMD_Thread_Manager SHALL provide a method to wait for all PMD threads to complete

### Requirement 6: PMD Thread Packet Processing Stub

**User Story:** As a developer, I want a stub packet processing function, so that the threading infrastructure can be tested before implementing actual packet processing.

#### Acceptance Criteria

1. THE PMD_Thread SHALL execute a packet processing loop function
2. THE packet processing loop function SHALL accept a PMD_Context as input
3. THE packet processing loop function SHALL be a stub that returns immediately (no actual packet processing)
4. THE packet processing loop function SHALL log the lcore_id and queue assignments when invoked

### Requirement 7: Main Lcore Control Plane Reservation

**User Story:** As a system architect, I want the main lcore reserved for control plane operations, so that control requests are not blocked by packet processing.

#### Acceptance Criteria

1. THE system SHALL identify the main lcore using rte_get_main_lcore()
2. THE system SHALL NOT assign PMD threads to the main lcore
3. THE main lcore SHALL be available for boost.asio unix-socket listener operations
4. WHEN the coremask contains only the main lcore, THE Config_Validator SHALL return an error indicating no worker lcores available
