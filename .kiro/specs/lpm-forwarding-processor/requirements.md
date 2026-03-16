# Requirements Document

## Introduction

This feature adds an LPM (Longest Prefix Match) forwarding processor to the DPDK packet processing pipeline. Modeled after DPDK's l3fwd example, the LPM_Forwarding_Processor parses incoming packets using the existing PacketMetadata infrastructure, performs an IPv4 destination-address lookup in a shared FIB (Forwarding Information Base) backed by DPDK's `rte_lpm` library, and forwards packets to the first configured TX queue. The FIB is a shared resource owned by the ControlPlane, loaded at startup from a file specified in the JSON configuration. The processor does not use SessionTable, FastLookupTable, or expose flow-level control-plane commands.

## Glossary

- **LPM_Forwarding_Processor**: A packet processor that performs longest-prefix-match forwarding using `rte_lpm`. Registered in the ProcessorRegistry under the name `"lpm_forwarding"`.
- **FIB**: Forwarding Information Base. A shared `rte_lpm` table mapping IPv4 destination prefixes (address/CIDR) to a next-hop identifier. Owned by the ControlPlane.
- **FIB_Loader**: A module responsible for parsing a FIB file and inserting prefix entries into the `rte_lpm` table. The file format and parsing logic are deferred to a later iteration; the initial implementation provides a stub function.
- **ControlPlane**: The main-lcore component that owns shared resources (RCU manager, SessionTable, FIB) and wires them into PMD thread ProcessorContexts before launch.
- **ProcessorContext**: The per-PMD-thread extensible context struct through which shared resources (stats, session table, FIB pointer) are passed to processors.
- **Config_Parser**: The JSON configuration parser that reads `DpdkConfig` from a file, including the new `fib_file` field.
- **PacketMetadata**: The existing 56-byte parsed header structure that extracts the 5-tuple and flags from raw packets.
- **Batch**: A fixed-size array of `rte_mbuf*` used for burst RX/TX operations.

## Requirements

### Requirement 1: JSON Configuration for FIB

**User Story:** As an operator, I want to specify a FIB file path in the JSON configuration, so that the system loads forwarding prefixes at startup.

#### Acceptance Criteria

1. WHEN the JSON configuration contains a `"fib_file"` field, THE Config_Parser SHALL parse the field value as a string and store it in `DpdkConfig::fib_file`.
2. WHEN the JSON configuration does not contain a `"fib_file"` field, THE Config_Parser SHALL set `DpdkConfig::fib_file` to an empty string.
3. WHEN the `"fib_file"` field is present but is not a string, THE Config_Parser SHALL return an `InvalidArgumentError` with a message identifying the field.

### Requirement 2: FIB Initialization by ControlPlane

**User Story:** As a system component, I want the ControlPlane to create and own the FIB, so that all PMD threads share a single LPM table.

#### Acceptance Criteria

1. WHEN `DpdkConfig::fib_file` is a non-empty string, THE ControlPlane SHALL create an `rte_lpm` table during initialization.
2. WHEN the `rte_lpm` table is created, THE ControlPlane SHALL invoke the FIB_Loader to populate the table from the file specified by `DpdkConfig::fib_file`.
3. IF `rte_lpm_create` fails, THEN THE ControlPlane SHALL return an error status and abort initialization.
4. WHEN `DpdkConfig::fib_file` is empty, THE ControlPlane SHALL skip FIB creation and leave the FIB pointer as null.
5. THE ControlPlane SHALL destroy the `rte_lpm` table during shutdown, after all PMD threads have stopped.

### Requirement 3: FIB Wiring into ProcessorContext

**User Story:** As a developer, I want the FIB pointer to be available in each PMD thread's ProcessorContext, so that the LPM_Forwarding_Processor can access the shared table without global state.

#### Acceptance Criteria

1. WHEN the FIB is created, THE ControlPlane SHALL set the `lpm_table` field in each PMD thread's ProcessorContext to point to the `rte_lpm` table.
2. WHEN the FIB is not created, THE ControlPlane SHALL leave the `lpm_table` field as null in each PMD thread's ProcessorContext.

### Requirement 4: ProcessorContext Extension

**User Story:** As a developer, I want the ProcessorContext to carry an LPM table pointer, so that processors can access the shared FIB without coupling to ControlPlane internals.

#### Acceptance Criteria

1. THE ProcessorContext SHALL contain a `void* lpm_table` field initialized to null.

### Requirement 5: FIB File Loading Stub

**User Story:** As a developer, I want a stub function for FIB file loading, so that the integration is complete and the actual parser can be implemented later.

#### Acceptance Criteria

1. THE FIB_Loader SHALL expose a function with signature `absl::Status LoadFibFile(const std::string& file_path, struct rte_lpm* lpm)`.
2. WHEN called, THE FIB_Loader SHALL return `OkStatus` without inserting any prefixes into the `rte_lpm` table.
3. THE FIB_Loader function signature SHALL accept a file path and an `rte_lpm` pointer, so that a future implementation can parse the file and call `rte_lpm_add` for each prefix entry.

### Requirement 6: LPM Forwarding Processor Registration

**User Story:** As an operator, I want to select the LPM forwarding processor by name in the JSON config, so that PMD threads use LPM-based forwarding.

#### Acceptance Criteria

1. THE LPM_Forwarding_Processor SHALL register itself in the ProcessorRegistry under the name `"lpm_forwarding"`.
2. WHEN a PMD thread's `processor` field is set to `"lpm_forwarding"`, THE ProcessorRegistry SHALL instantiate and launch the LPM_Forwarding_Processor for that thread.

### Requirement 7: LPM Forwarding Processor Queue Validation

**User Story:** As a developer, I want the processor to validate its queue configuration at startup, so that misconfiguration is caught early.

#### Acceptance Criteria

1. WHEN the TX queue list is empty, THE LPM_Forwarding_Processor SHALL return an `InvalidArgumentError` from `check_impl`.
2. WHEN the TX queue list contains at least one entry, THE LPM_Forwarding_Processor SHALL return `OkStatus` from `check_impl`.

### Requirement 8: Packet Parsing in LPM Forwarding Processor

**User Story:** As a developer, I want each received packet to be parsed using PacketMetadata, so that the destination IP is available for LPM lookup.

#### Acceptance Criteria

1. WHEN a batch of packets is received, THE LPM_Forwarding_Processor SHALL call `PacketMetadata::Parse` for each packet in the batch.
2. WHEN `PacketMetadata::Parse` returns a non-OK result for a packet, THE LPM_Forwarding_Processor SHALL skip the LPM lookup for that packet and continue forwarding the packet.

### Requirement 9: LPM Lookup per Packet

**User Story:** As a developer, I want each successfully parsed IPv4 packet to be looked up in the FIB, so that the forwarding decision is based on the longest prefix match.

#### Acceptance Criteria

1. WHEN a packet is successfully parsed and the destination address is IPv4, THE LPM_Forwarding_Processor SHALL call `rte_lpm_lookup` with the destination IPv4 address from PacketMetadata.
2. WHEN `rte_lpm_lookup` returns a match, THE LPM_Forwarding_Processor SHALL record the next-hop value (for future use) and continue to forward the packet.
3. WHEN `rte_lpm_lookup` returns no match, THE LPM_Forwarding_Processor SHALL continue to forward the packet (default forwarding behavior).
4. WHEN the `lpm_table` pointer in ProcessorContext is null, THE LPM_Forwarding_Processor SHALL skip the LPM lookup and forward the packet without lookup.
5. WHEN the packet is IPv6, THE LPM_Forwarding_Processor SHALL skip the LPM lookup and forward the packet (IPv6 LPM is out of scope).

### Requirement 10: Packet Forwarding to First TX Queue

**User Story:** As a developer, I want all packets to be forwarded to the first configured TX queue, so that the processor integrates with the existing TX pipeline.

#### Acceptance Criteria

1. THE LPM_Forwarding_Processor SHALL transmit each batch of packets using `rte_eth_tx_burst` on the first TX queue from the configuration.
2. WHEN `rte_eth_tx_burst` does not transmit all packets in the batch, THE LPM_Forwarding_Processor SHALL free the untransmitted mbufs using `rte_pktmbuf_free`.

### Requirement 11: Packet Statistics Recording

**User Story:** As an operator, I want per-thread packet and byte counters for the LPM processor, so that I can monitor throughput.

#### Acceptance Criteria

1. WHEN a batch of packets is received, THE LPM_Forwarding_Processor SHALL record the packet count and total byte count using the PacketStats interface before transmitting.
2. WHEN the stats pointer is null, THE LPM_Forwarding_Processor SHALL skip statistics recording.

### Requirement 12: Parameter Validation

**User Story:** As a developer, I want the LPM processor to reject unrecognized configuration parameters, so that typos in the config are caught at startup.

#### Acceptance Criteria

1. WHEN `CheckParams` is called with an empty parameter map, THE LPM_Forwarding_Processor SHALL return `OkStatus`.
2. WHEN `CheckParams` is called with any non-empty parameter map, THE LPM_Forwarding_Processor SHALL return an `InvalidArgumentError` identifying the unrecognized key.

### Requirement 13: No FastLookupTable or Session Usage

**User Story:** As a developer, I want the LPM processor to operate without FastLookupTable or SessionTable dependencies, so that the processor is lightweight and focused on L3 forwarding.

#### Acceptance Criteria

1. THE LPM_Forwarding_Processor SHALL NOT instantiate or reference a FastLookupTable.
2. THE LPM_Forwarding_Processor SHALL NOT read or write the `session_table` field in ProcessorContext.
3. THE LPM_Forwarding_Processor SHALL NOT register any control-plane commands (no `get_flow` or similar commands).
