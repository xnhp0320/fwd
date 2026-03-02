# Requirements Document

## Introduction

This feature adds per-PMD-thread packet statistics tracking to the DPDK packet processing application, a control plane command to query those statistics, and end-to-end tests that use Scapy for packet generation and PTF for packet verification. The stats module lives inside the `processor/` directory (no new directory), tracks bytes and packets per PMD thread, and is readable from the control plane. The e2e tests send packets through virtual TAP devices, verify the simple forwarding processor echoes them back, and confirm the stats command reports correct counters.

## Glossary

- **Stats_Module**: A C++ module in `processor/` that maintains per-PMD-thread packet and byte counters.
- **PMD_Thread**: A DPDK poll-mode driver thread running on a dedicated lcore, executing a packet processor hot loop.
- **Control_Plane**: The main-lcore event loop that accepts Unix socket commands and orchestrates PMD threads.
- **Command_Handler**: The component that parses JSON commands, dispatches them, and formats JSON responses.
- **Simple_Forwarding_Processor**: The existing CRTP-based processor that drains all RX queues into a single TX queue.
- **Scapy**: A Python packet crafting library used to generate and parse network packets in tests.
- **PTF**: Packet Test Framework, a Python library for data-plane testing that provides packet send/receive/verify utilities.
- **TAP_Device**: A virtual network interface (`dtap0`, `dtap1`) created by DPDK's `net_tap` PMD, accessible from the host OS.
- **Stats_Command**: A new control plane JSON command (`get_stats`) that returns per-thread and total packet/byte counters.

## Requirements

### Requirement 1: Per-PMD-Thread Stats Counters

**User Story:** As a network operator, I want each PMD thread to maintain its own packet and byte counters, so that I can monitor per-thread traffic independently.

#### Acceptance Criteria

1. THE Stats_Module SHALL maintain a separate packet counter and byte counter for each PMD_Thread.
2. WHEN a PMD_Thread processes a batch of packets, THE Stats_Module SHALL increment the packet counter by the number of packets in the batch.
3. WHEN a PMD_Thread processes a batch of packets, THE Stats_Module SHALL increment the byte counter by the total byte length of all packets in the batch.
4. THE Stats_Module SHALL store counters using 64-bit unsigned integers to avoid overflow during sustained traffic.
5. THE Stats_Module SHALL reside in the `processor/` directory alongside existing processor code, not in a separate directory.

### Requirement 2: Stats Integration with Processor Hot Loop

**User Story:** As a developer, I want the stats counters to be updated inside the processor hot loop, so that every processed packet is counted without additional polling overhead.

#### Acceptance Criteria

1. WHEN the Simple_Forwarding_Processor completes an RX burst, THE Simple_Forwarding_Processor SHALL update the Stats_Module counters for the current PMD_Thread with the received packet count and total byte length before transmitting.
2. THE Stats_Module SHALL provide a thread-safe read interface so the Control_Plane can read counters from any PMD_Thread without corrupting the counters.
3. WHILE a PMD_Thread is running, THE Stats_Module SHALL allow the Control_Plane to read that thread's counters concurrently without blocking the hot loop.

### Requirement 3: Control Plane Stats Command

**User Story:** As a network operator, I want a control plane command that shows per-thread and total packet/byte statistics, so that I can monitor traffic through the application.

#### Acceptance Criteria

1. WHEN the Command_Handler receives a `get_stats` command, THE Command_Handler SHALL return a JSON response containing per-thread statistics and total statistics.
2. THE Command_Handler SHALL include in the per-thread statistics: the lcore ID, packet count, and byte count for each PMD_Thread.
3. THE Command_Handler SHALL include in the total statistics: the sum of packet counts and the sum of byte counts across all PMD_Threads.
4. WHEN the Command_Handler receives a `get_stats` command and no PMD_Threads are running, THE Command_Handler SHALL return an empty per-thread array and zero totals.
5. THE Command_Handler SHALL format the `get_stats` response as:
   ```json
   {
     "status": "success",
     "result": {
       "threads": [
         {"lcore_id": 1, "packets": 100, "bytes": 6400},
         {"lcore_id": 2, "packets": 50, "bytes": 3200}
       ],
       "total": {"packets": 150, "bytes": 9600}
     }
   }
   ```
6. IF the Command_Handler receives an unknown command, THEN THE Command_Handler SHALL return an error response (existing behavior preserved).

### Requirement 4: Control Client Stats Method

**User Story:** As a test developer, I want a Python helper method in the control client fixture to call the stats command, so that e2e tests can easily query statistics.

#### Acceptance Criteria

1. THE Control_Client SHALL provide a `get_stats()` method that sends the `get_stats` command and returns the parsed JSON response.
2. WHEN the `get_stats()` method is called, THE Control_Client SHALL return a dictionary containing `status`, and `result` with `threads` and `total` fields.

### Requirement 5: E2E Test Dependencies (Scapy and PTF)

**User Story:** As a test developer, I want Scapy and PTF available in the test environment, so that I can craft packets and verify forwarded packets programmatically.

#### Acceptance Criteria

1. THE test environment SHALL have Scapy installed via `apt install python3-scapy` or `pip install scapy`.
2. THE test environment SHALL have PTF installed via `pip install ptf`.
3. THE test requirements file SHALL list Scapy and PTF as dependencies.

### Requirement 6: E2E Packet Send and Receive via TAP Device

**User Story:** As a test developer, I want to send crafted packets into a TAP device and receive the echoed packets from the same TAP device, so that I can verify the simple forwarding processor works end-to-end.

#### Acceptance Criteria

1. WHEN a test sends a crafted Ethernet packet into a TAP_Device using Scapy, THE Simple_Forwarding_Processor SHALL forward the packet back out through the same TAP_Device.
2. THE test SHALL use Scapy's `sendp()` to inject packets at Layer 2 into the TAP_Device.
3. THE test SHALL use Scapy's `sniff()` or PTF's receive utilities to capture packets returned from the TAP_Device.
4. WHEN a packet is sent and received, THE test SHALL use PTF to verify the received packet matches the sent packet in protocol fields and payload.
5. IF no packet is received within a configurable timeout, THEN THE test SHALL fail with a descriptive error message.
6. THE test SHALL bring the TAP_Device interface up before sending packets, using `ip link set <interface> up`.

### Requirement 7: E2E Stats Verification Test

**User Story:** As a test developer, I want to verify that the stats command reports correct counters after sending known traffic, so that I can confirm the stats module works end-to-end.

#### Acceptance Criteria

1. WHEN a known number of packets with known sizes are sent through the TAP_Device, THE Stats_Command SHALL report a total packet count equal to the number of sent packets.
2. WHEN a known number of packets with known sizes are sent through the TAP_Device, THE Stats_Command SHALL report a total byte count equal to the sum of all sent packet sizes.
3. THE test SHALL query stats before sending packets and verify the counters start at zero (or record the baseline).
4. THE test SHALL query stats after sending packets and verify the counters increased by the expected amounts.
5. WHEN multiple PMD_Threads are configured, THE Stats_Command SHALL report per-thread counters that sum to the total counters.

### Requirement 8: E2E Test Integration with Existing Framework

**User Story:** As a test developer, I want the new e2e packet tests to integrate with the existing pytest framework and fixtures, so that they run alongside existing tests without duplication.

#### Acceptance Criteria

1. THE new e2e test file SHALL be located at `tests/e2e/test_packet_stats.py`.
2. THE new e2e tests SHALL reuse the existing `dpdk_process`, `control_client`, `tap_interfaces`, and `test_config` pytest fixtures.
3. THE new e2e tests SHALL follow the same test class and method naming conventions as existing e2e tests.
4. WHEN the test suite is run with `pytest tests/e2e/`, THE new tests SHALL execute alongside existing tests without conflicts.
