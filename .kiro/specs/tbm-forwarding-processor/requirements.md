# Requirements Document

## Introduction

This feature integrates the `tbmlib` third-party library (Tree Bitmap based IP prefix lookup) as an alternative forwarding backend alongside the existing `rte_lpm`-based LPM_Forwarding_Processor. A new TBM_Forwarding_Processor mirrors the LPM processor's architecture — CRTP base, self-registration, burst RX/TX, PacketMetadata parsing, per-thread stats — but delegates longest-prefix-match lookups to `tbm_lookup()` instead of `rte_lpm_lookup()`. A new TBM-aware FIB loader (`LoadFibFileToTbm`) populates the TBM table at startup using `tbm_insert()`. The ControlPlane owns the `FibTbm` instance, wires it into each PMD thread's ProcessorContext via a new `tbm_table` field, and destroys it on shutdown. Operators select the backend by setting `"processor": "tbm_forwarding"` in the JSON configuration.

## Glossary

- **TBM_Forwarding_Processor**: A packet processor that performs longest-prefix-match forwarding using `tbm_lookup()` from the tbmlib library. Registered in the ProcessorRegistry under the name `"tbm_forwarding"`.
- **FibTbm**: The Tree Bitmap table type (`tbmlib__Tbm$5$uint$uint$__`) from tbmlib. Stores IPv4 prefixes and supports lookup, insert, remove, and iterate operations.
- **TBM_FIB_Loader**: A module responsible for parsing a FIB file and inserting prefix entries into a `FibTbm` table using `tbm_insert()`.
- **FibCidr**: The prefix type (`tbmlib_cidr__Cidr$uint$__`) from tbmlib, containing an `ip` (uint32_t) and `cidr` (uint32_t) field.
- **ControlPlane**: The main-lcore component that owns shared resources (RCU manager, SessionTable, LPM FIB, TBM FIB) and wires them into PMD thread ProcessorContexts before launch.
- **ProcessorContext**: The per-PMD-thread extensible context struct through which shared resources (stats, session table, LPM table, TBM table) are passed to processors.
- **LPM_Forwarding_Processor**: The existing processor that uses DPDK's `rte_lpm` for longest-prefix-match lookups. The TBM_Forwarding_Processor is modeled after this processor.
- **PacketMetadata**: The existing parsed header structure that extracts the 5-tuple and flags from raw packets.
- **Batch**: A fixed-size array of `rte_mbuf*` used for burst RX/TX operations.
- **ProcessorRegistry**: The singleton registry that maps processor name strings to launcher/checker/param-checker entries.

## Requirements

### Requirement 1: ProcessorContext Extension for TBM Table

**User Story:** As a developer, I want the ProcessorContext to carry a TBM table pointer, so that the TBM_Forwarding_Processor can access the shared FibTbm without coupling to ControlPlane internals.

#### Acceptance Criteria

1. THE ProcessorContext SHALL contain a `void* tbm_table` field initialized to null.
2. THE new `tbm_table` field SHALL NOT affect the existing `lpm_table`, `session_table`, or `processor_data` fields.

### Requirement 2: TBM FIB Initialization by ControlPlane

**User Story:** As a system component, I want the ControlPlane to create and own the TBM FIB, so that all PMD threads running the TBM processor share a single TBM table.

#### Acceptance Criteria

1. WHEN `DpdkConfig::fib_file` is a non-empty string, THE ControlPlane SHALL allocate a `FibTbm` instance and call `tbm_init()` to initialize the TBM table during initialization.
2. WHEN the `FibTbm` is initialized, THE ControlPlane SHALL invoke the TBM_FIB_Loader to populate the table from the file specified by `DpdkConfig::fib_file`.
3. IF `tbm_init()` or the TBM_FIB_Loader returns an error, THEN THE ControlPlane SHALL call `tbm_free()` on the FibTbm, return an error status, and abort initialization.
4. WHEN `DpdkConfig::fib_file` is empty, THE ControlPlane SHALL skip TBM FIB creation and leave the TBM table pointer as null.
5. THE ControlPlane SHALL call `tbm_free()` on the FibTbm during shutdown, after all PMD threads have stopped.

### Requirement 3: TBM FIB Wiring into ProcessorContext

**User Story:** As a developer, I want the TBM FIB pointer to be available in each PMD thread's ProcessorContext, so that the TBM_Forwarding_Processor can access the shared table without global state.

#### Acceptance Criteria

1. WHEN the TBM FIB is created, THE ControlPlane SHALL set the `tbm_table` field in each PMD thread's ProcessorContext to point to the `FibTbm` instance.
2. WHEN the TBM FIB is not created, THE ControlPlane SHALL leave the `tbm_table` field as null in each PMD thread's ProcessorContext.

### Requirement 4: TBM-Aware FIB File Loader

**User Story:** As a developer, I want a FIB loader that populates a TBM table from a FIB file, so that the TBM_Forwarding_Processor has prefix data for lookups.

#### Acceptance Criteria

1. THE TBM_FIB_Loader SHALL expose a function with signature `absl::Status LoadFibFileToTbm(const std::string& file_path, FibTbm* tbm, uint32_t* rules_loaded)`.
2. WHEN called with a valid FIB file, THE TBM_FIB_Loader SHALL parse each line pair (IPv4 address, CIDR prefix length) and call `tbm_insert()` with a `FibCidr` containing the IP in host byte order and the CIDR length.
3. WHEN the FIB file cannot be opened, THE TBM_FIB_Loader SHALL return a `NotFoundError` identifying the file path.
4. WHEN a line contains an invalid IPv4 address, THE TBM_FIB_Loader SHALL return an `InvalidArgumentError` identifying the line number and invalid value.
5. WHEN a line contains a CIDR length outside the range 0-32, THE TBM_FIB_Loader SHALL return an `InvalidArgumentError` identifying the line number and invalid value.
6. WHEN `tbm_insert()` returns a non-null fault, THE TBM_FIB_Loader SHALL return an `InternalError` identifying the prefix that failed.
7. WHEN `rules_loaded` is non-null, THE TBM_FIB_Loader SHALL store the number of successfully inserted prefixes.
8. WHEN the `tbm` pointer is null, THE TBM_FIB_Loader SHALL return an `InvalidArgumentError`.

### Requirement 5: TBM Forwarding Processor Registration

**User Story:** As an operator, I want to select the TBM forwarding processor by name in the JSON config, so that PMD threads use TBM-based forwarding.

#### Acceptance Criteria

1. THE TBM_Forwarding_Processor SHALL register itself in the ProcessorRegistry under the name `"tbm_forwarding"`.
2. WHEN a PMD thread's `processor` field is set to `"tbm_forwarding"`, THE ProcessorRegistry SHALL instantiate and launch the TBM_Forwarding_Processor for that thread.

### Requirement 6: TBM Forwarding Processor Queue Validation

**User Story:** As a developer, I want the TBM processor to validate its queue configuration at startup, so that misconfiguration is caught early.

#### Acceptance Criteria

1. WHEN the TX queue list is empty, THE TBM_Forwarding_Processor SHALL return an `InvalidArgumentError` from `check_impl`.
2. WHEN the TX queue list contains at least one entry, THE TBM_Forwarding_Processor SHALL return `OkStatus` from `check_impl`.

### Requirement 7: Packet Parsing in TBM Forwarding Processor

**User Story:** As a developer, I want each received packet to be parsed using PacketMetadata, so that the destination IP is available for TBM lookup.

#### Acceptance Criteria

1. WHEN a batch of packets is received, THE TBM_Forwarding_Processor SHALL call `PacketMetadata::Parse` for each packet in the batch.
2. WHEN `PacketMetadata::Parse` returns a non-OK result for a packet, THE TBM_Forwarding_Processor SHALL skip the TBM lookup for that packet and continue forwarding the packet.

### Requirement 8: TBM Lookup per Packet

**User Story:** As a developer, I want each successfully parsed IPv4 packet to be looked up in the TBM FIB, so that the forwarding decision is based on the longest prefix match.

#### Acceptance Criteria

1. WHEN a packet is successfully parsed and the destination address is IPv4, THE TBM_Forwarding_Processor SHALL call `tbm_lookup()` with the destination IPv4 address in host byte order from PacketMetadata.
2. WHEN `tbm_lookup()` returns a null fault (success), THE TBM_Forwarding_Processor SHALL record the next-hop value (for future use) and continue to forward the packet.
3. WHEN `tbm_lookup()` returns a non-null fault (no match), THE TBM_Forwarding_Processor SHALL continue to forward the packet (default forwarding behavior).
4. WHEN the `tbm_table` pointer in ProcessorContext is null, THE TBM_Forwarding_Processor SHALL skip the TBM lookup and forward the packet without lookup.
5. WHEN the packet is IPv6, THE TBM_Forwarding_Processor SHALL skip the TBM lookup and forward the packet (IPv6 TBM lookup is out of scope).

### Requirement 9: Packet Forwarding to First TX Queue

**User Story:** As a developer, I want all packets to be forwarded to the first configured TX queue, so that the TBM processor integrates with the existing TX pipeline.

#### Acceptance Criteria

1. THE TBM_Forwarding_Processor SHALL transmit each batch of packets using `rte_eth_tx_burst` on the first TX queue from the configuration.
2. WHEN `rte_eth_tx_burst` does not transmit all packets in the batch, THE TBM_Forwarding_Processor SHALL free the untransmitted mbufs using `rte_pktmbuf_free`.

### Requirement 10: Packet Statistics Recording

**User Story:** As an operator, I want per-thread packet and byte counters for the TBM processor, so that I can monitor throughput.

#### Acceptance Criteria

1. WHEN a batch of packets is received, THE TBM_Forwarding_Processor SHALL record the packet count and total byte count using the PacketStats interface before transmitting.
2. WHEN the stats pointer is null, THE TBM_Forwarding_Processor SHALL skip statistics recording.

### Requirement 11: Parameter Validation

**User Story:** As a developer, I want the TBM processor to reject unrecognized configuration parameters, so that typos in the config are caught at startup.

#### Acceptance Criteria

1. WHEN `CheckParams` is called with an empty parameter map, THE TBM_Forwarding_Processor SHALL return `OkStatus`.
2. WHEN `CheckParams` is called with any non-empty parameter map, THE TBM_Forwarding_Processor SHALL return an `InvalidArgumentError` identifying the unrecognized key.

### Requirement 12: No FastLookupTable or Session Usage

**User Story:** As a developer, I want the TBM processor to operate without FastLookupTable or SessionTable dependencies, so that the processor is lightweight and focused on L3 forwarding.

#### Acceptance Criteria

1. THE TBM_Forwarding_Processor SHALL NOT instantiate or reference a FastLookupTable.
2. THE TBM_Forwarding_Processor SHALL NOT read or write the `session_table` field in ProcessorContext.
3. THE TBM_Forwarding_Processor SHALL NOT register any control-plane commands.

### Requirement 13: Bazel Build Integration

**User Story:** As a developer, I want the TBM forwarding processor and TBM FIB loader to build correctly with Bazel, so that the feature integrates into the existing build system.

#### Acceptance Criteria

1. THE TBM_Forwarding_Processor Bazel target SHALL depend on `//tbm:tbmlib` for access to the tbmlib API.
2. THE TBM_Forwarding_Processor Bazel target SHALL use `alwayslink = True` to ensure the `REGISTER_PROCESSOR` static initializer is not stripped by the linker.
3. THE TBM_FIB_Loader Bazel target SHALL depend on `//tbm:tbmlib` for access to `tbm_insert()` and the `FibCidr` type.
4. THE main binary Bazel target SHALL depend on the TBM_Forwarding_Processor target so that the processor is linked and registered at startup.

### Requirement 14: Coexistence with LPM Forwarding Processor

**User Story:** As an operator, I want both LPM and TBM forwarding processors to be available simultaneously, so that I can choose the backend per PMD thread via configuration.

#### Acceptance Criteria

1. THE TBM_Forwarding_Processor and the LPM_Forwarding_Processor SHALL both be registered in the ProcessorRegistry at startup without conflict.
2. WHEN `DpdkConfig::fib_file` is non-empty, THE ControlPlane SHALL initialize both the `rte_lpm` table and the `FibTbm` table from the same FIB file.
3. WHEN different PMD threads specify `"lpm_forwarding"` and `"tbm_forwarding"` respectively, THE system SHALL launch each thread with the correct processor type using the corresponding table from ProcessorContext.
