# Requirements Document

## Introduction

This feature enables end-to-end testing of the FiveTupleForwardingProcessor by:
1. Extending the `get_flow_table` control command to read and serialize actual flow table entries from each PMD thread's FastLookupTable.
2. Extending `generate_dpdk_config.py` and `TestConfigGenerator` to include `processor_name` and `processor_params` in generated configurations.
3. Adding e2e tests that send crafted packets via TAP interfaces using scapy and verify the resulting flow table entries via the `get_flow_table` command.

## Glossary

- **ControlPlane**: The main-lcore event loop that handles Unix socket commands and orchestrates PMD thread management.
- **CommandHandler**: The component that parses JSON commands received over the Unix socket and dispatches them to handler functions.
- **PMDThreadManager**: The manager that owns and provides access to all PmdThread instances, keyed by lcore ID.
- **PmdThread**: A worker thread running on a dedicated lcore that executes a packet processor's hot loop.
- **ProcessorContext**: A struct passed to processor launchers containing per-thread resources (currently `PacketStats*`). Extended in this feature to also carry a `void*` pointer for processor-specific data.
- **FiveTupleForwardingProcessor**: A packet processor that parses incoming packets, extracts five-tuple metadata, and inserts entries into a FastLookupTable.
- **FastLookupTable**: A high-performance hash-set-backed flow table with `SetModifiable(bool)` for cross-thread read coordination, and `Begin()`/`End()` iterators for entry enumeration.
- **LookupEntry**: A cache-line-aligned struct representing a single flow entry with fields: `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `vni`, `flags`.
- **TestConfigGenerator**: A Python class that generates `dpdk.json` configuration files for e2e testing with net_tap virtual PMD.
- **ControlClient**: A Python class that sends JSON commands to the ControlPlane over a Unix domain socket and parses responses.
- **GracePeriod**: An RCU grace period initiated via `RcuManager::CallAfterGracePeriod` after calling `SetModifiable(false)`. The callback fires once all PMD threads have passed through a quiescent state (completed their current `process_impl()` iteration), guaranteeing the table is safe to read.

## Requirements

### Requirement 1: Extend ProcessorContext with Processor-Specific Data Pointer

**User Story:** As a control plane developer, I want ProcessorContext to carry a generic pointer so that processors can expose internal data structures to the control plane without coupling ProcessorContext to specific processor types.

#### Acceptance Criteria

1. THE ProcessorContext SHALL contain a `void* processor_data` field initialized to `nullptr`.
2. WHEN a processor launcher constructs a FiveTupleForwardingProcessor, THE launcher SHALL store a pointer to the processor's FastLookupTable in `ProcessorContext::processor_data`.
3. THE ProcessorContext SHALL remain backward-compatible such that existing processors that do not set `processor_data` continue to function without modification.

### Requirement 2: Expose ProcessorContext from PmdThread

**User Story:** As a control plane developer, I want to access each PMD thread's ProcessorContext so that the CommandHandler can reach processor-specific data for each thread.

#### Acceptance Criteria

1. THE PmdThread SHALL store the ProcessorContext used by its processor launcher as a member accessible via a `GetProcessorContext()` const accessor.
2. WHEN `PmdThread::Run()` invokes the processor launcher, THE PmdThread SHALL pass a reference to its stored ProcessorContext so that the launcher can populate `processor_data`.
3. THE PMDThreadManager SHALL provide access to each PmdThread via the existing `GetThread(lcore_id)` method, enabling the CommandHandler to iterate all threads and read their ProcessorContext.

### Requirement 3: Extend get_flow_table to Serialize Flow Table Entries

**User Story:** As a test author, I want the `get_flow_table` command to return the actual flow table entries (not just a count) so that e2e tests can verify that specific packets produced the correct five-tuple entries.

#### Acceptance Criteria

1. WHEN the `get_flow_table` command is received, THE CommandHandler SHALL iterate all PmdThread instances via PMDThreadManager.
2. WHEN reading a thread's FastLookupTable, THE CommandHandler SHALL call `SetModifiable(false)` on the table before reading.
3. WHEN `SetModifiable(false)` has been called, THE CommandHandler SHALL use `RcuManager::CallAfterGracePeriod` to schedule the table read after all PMD threads have passed through a quiescent state.
4. WHEN the GracePeriod has elapsed, THE CommandHandler SHALL iterate the FastLookupTable entries using `Begin()` and `End()` and serialize each LookupEntry to JSON.
5. WHEN all entries from a thread's table have been read, THE CommandHandler SHALL call `SetModifiable(true)` to re-enable modifications.
6. THE CommandHandler SHALL return a JSON response containing a `threads` array, where each element includes the `lcore_id` and an `entries` array of serialized LookupEntry objects.
7. THE serialized LookupEntry JSON SHALL include fields: `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `vni`, and `is_ipv6`.
8. IF `processor_data` is `nullptr` for a given thread (processor is not FiveTupleForwarding), THEN THE CommandHandler SHALL skip that thread or return an empty entries array for the thread.
9. IF an error occurs during table reading, THEN THE CommandHandler SHALL call `SetModifiable(true)` to restore the table to a modifiable state before returning an error response.

### Requirement 4: Extend Configuration Generator with Processor Settings

**User Story:** As a test author, I want `TestConfigGenerator` and `generate_dpdk_config.py` to support `processor_name` and `processor_params` so that e2e tests can launch the FiveTupleForwardingProcessor.

#### Acceptance Criteria

1. WHEN `processor_name` is provided to `TestConfigGenerator.generate_config()`, THE TestConfigGenerator SHALL include a `processor_name` field in each `pmd_threads` entry of the generated JSON.
2. WHEN `processor_params` is provided to `TestConfigGenerator.generate_config()`, THE TestConfigGenerator SHALL include a `processor_params` object in each `pmd_threads` entry of the generated JSON.
3. WHEN `processor_name` is not provided, THE TestConfigGenerator SHALL omit the `processor_name` field from the generated JSON, preserving backward compatibility with existing tests.
4. WHEN `processor_name` is provided to `generate_dpdk_config.py` via a `--processor` CLI argument, THE script SHALL pass the value to `TestConfigGenerator.generate_config()`.
5. WHEN `processor_params` key-value pairs are provided to `generate_dpdk_config.py` via `--processor-param KEY=VALUE` CLI arguments, THE script SHALL pass them as a dictionary to `TestConfigGenerator.generate_config()`.

### Requirement 5: E2E Test — Single IPv4 Packet Produces Correct Flow Entry

**User Story:** As a test author, I want to verify that sending a single IPv4 packet through a TAP interface results in a correct five-tuple entry in the flow table.

#### Acceptance Criteria

1. WHEN a single IPv4/TCP packet with known src_ip, dst_ip, src_port, dst_port is sent via a TAP interface, THE flow table (read via `get_flow_table`) SHALL contain an entry matching the sent packet's five-tuple fields.
2. THE test SHALL configure the DPDK process with `processor_name` set to `five_tuple_forwarding`.
3. THE test SHALL use scapy to construct and send the packet, and ControlClient to issue the `get_flow_table` command.

### Requirement 6: E2E Test — Multiple Distinct Packets Produce Distinct Entries

**User Story:** As a test author, I want to verify that sending multiple packets with different five-tuples results in distinct flow table entries for each.

#### Acceptance Criteria

1. WHEN multiple IPv4 packets with distinct five-tuples are sent via a TAP interface, THE flow table SHALL contain one entry per unique five-tuple.
2. THE test SHALL verify that the total number of entries across all threads equals the number of distinct five-tuples sent.

### Requirement 7: E2E Test — Duplicate Packets Do Not Create Extra Entries

**User Story:** As a test author, I want to verify that sending duplicate packets (same five-tuple) does not create additional flow table entries.

#### Acceptance Criteria

1. WHEN multiple packets with the same five-tuple are sent via a TAP interface, THE flow table SHALL contain exactly one entry for that five-tuple.
2. THE test SHALL send at least two packets with identical five-tuples and verify the entry count remains one for that flow.

### Requirement 8: E2E Test — IPv6 Packet Produces Correct Flow Entry

**User Story:** As a test author, I want to verify that IPv6 packets are correctly parsed and produce flow table entries with `is_ipv6` set to true.

#### Acceptance Criteria

1. WHEN a single IPv6/TCP packet with known addresses and ports is sent via a TAP interface, THE flow table SHALL contain an entry with `is_ipv6` set to `true` and matching source/destination addresses and ports.

### Requirement 9: E2E Test — get_flow_table Response Structure Validation

**User Story:** As a test author, I want to verify that the `get_flow_table` response has the expected JSON structure so that tests can reliably parse it.

#### Acceptance Criteria

1. WHEN `get_flow_table` is called on a running DPDK process with FiveTupleForwardingProcessor, THE response SHALL have `status` equal to `success`.
2. THE response `result` SHALL contain a `threads` array where each element has `lcore_id` (integer) and `entries` (array).
3. WHEN entries exist, each entry in the `entries` array SHALL contain `src_ip` (string), `dst_ip` (string), `src_port` (integer), `dst_port` (integer), `protocol` (integer), `vni` (integer), and `is_ipv6` (boolean).
