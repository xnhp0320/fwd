# Implementation Plan: E2E Five-Tuple Forwarding

## Overview

Implement end-to-end testing of the FiveTupleForwardingProcessor by extending ProcessorContext with a generic data pointer, promoting it to a PmdThread member, rewriting `get_flow_table` to serialize actual flow entries via RCU grace periods, extending the Python test config generator with processor settings, and adding e2e tests that send crafted packets and verify flow table state.

Changes are ordered: data plane context first, then control plane async response, then Python infrastructure, then e2e tests.

## Tasks

- [x] 1. Extend ProcessorContext and PmdThread
  - [x] 1.1 Add `void* processor_data` to ProcessorContext
    - In `processor/processor_context.h`, add `void* processor_data = nullptr` field to the `ProcessorContext` struct
    - Existing processors that don't set it continue to work (nullptr default)
    - _Requirements: 1.1, 1.3_

  - [x] 1.2 Add `ExportProcessorData` to PacketProcessorBase and FiveTupleForwardingProcessor
    - Add a default no-op `void ExportProcessorData(ProcessorContext&) {}` to `PacketProcessorBase`
    - Override in `FiveTupleForwardingProcessor` to set `ctx.processor_data = &table_`
    - _Requirements: 1.2_

  - [x] 1.3 Change LauncherFn signature to non-const reference
    - In `processor/processor_registry.h`, change `const processor::ProcessorContext& ctx` to `processor::ProcessorContext& ctx` in the `LauncherFn` type alias
    - Update the `MakeProcessorEntry` template lambda to accept `ProcessorContext&` (non-const)
    - Call `proc.ExportProcessorData(ctx)` after constructing the processor in the launcher lambda
    - _Requirements: 1.2, 2.2_

  - [x] 1.4 Promote ProcessorContext to PmdThread member
    - In `config/pmd_thread.h`, add `processor::ProcessorContext ctx_` member and `const processor::ProcessorContext& GetProcessorContext() const` accessor
    - Initialize `ctx_.stats = &stats_` in the PmdThread constructor
    - In `config/pmd_thread.cc`, change `Run()` to pass `ctx_` by reference to the launcher instead of constructing a local temporary
    - _Requirements: 2.1, 2.2, 2.3_

  - [ ]* 1.5 Write unit tests for ProcessorContext and ExportProcessorData
    - Verify `ProcessorContext` default-constructs with `processor_data == nullptr`
    - Construct a `FiveTupleForwardingProcessor`, call `ExportProcessorData`, verify `processor_data` is non-null
    - Verify `SimpleForwardingProcessor`'s `ExportProcessorData` leaves `processor_data` as nullptr
    - _Requirements: 1.1, 1.2, 1.3_

- [x] 2. Checkpoint — Data plane context changes
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Rewrite HandleGetFlowTable with async RCU grace period
  - [x] 3.1 Add RcuManager dependency and async response support to CommandHandler
    - Add `rcu::RcuManager* rcu_manager_` member to `CommandHandler` (set via constructor parameter or setter)
    - Add `using ResponseCallback = std::function<void(const std::string&)>` type alias
    - Change `HandleCommand` signature to `std::optional<std::string> HandleCommand(const std::string& json_command, ResponseCallback response_cb = nullptr)` — returns `std::nullopt` for deferred (async) commands
    - Remove `flow_table_query_` member and `SetFlowTableQueryCallback` method
    - Update `control/BUILD` deps to include `//rcu:rcu_manager`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

  - [x] 3.2 Implement two-phase HandleGetFlowTableAsync
    - Create `void HandleGetFlowTableAsync(ResponseCallback response_cb)` private method
    - Phase 1: iterate all threads via `thread_manager_->GetLcoreIds()` and `GetThread()`, collect `FastLookupTable*` from `GetProcessorContext().processor_data` (skip nullptr), call `SetModifiable(false)` on each table, then call `rcu_manager_->CallAfterGracePeriod(callback)`
    - Phase 2 (in grace period callback): iterate each table's entries via `Begin()`/`End()`, serialize each `LookupEntry` to JSON using `inet_ntop` for IP addresses (AF_INET for IPv4, AF_INET6 for IPv6 based on `IsIpv6()`), include fields `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `vni`, `is_ipv6`
    - Call `SetModifiable(true)` on each table after reading (with try/catch to guarantee restoration on error)
    - Build response with `threads` array (each element has `lcore_id` and `entries`), invoke `response_cb`
    - Threads with nullptr `processor_data` get empty `entries` array
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

  - [x] 3.3 Wire async response in ControlPlane
    - In `control/control_plane.cc`, update the socket server callback to pass the `response_callback` through to `HandleCommand`
    - If `HandleCommand` returns a value, call `response_callback` with it; if `std::nullopt`, the handler will call `response_callback` later
    - Pass `rcu_manager_.get()` to `CommandHandler` constructor or setter
    - _Requirements: 3.1, 3.3_

  - [ ]* 3.4 Write unit tests for HandleGetFlowTable serialization
    - Test with nullptr `processor_data`: verify empty entries array in response
    - Test with known entries in a FastLookupTable: verify JSON fields match (src_ip, dst_ip, src_port, dst_port, protocol, vni, is_ipv6)
    - Test IPv4 and IPv6 address formatting via inet_ntop
    - Test `SetModifiable(true)` is called even if iteration throws
    - Update `control/command_handler_test.cc`
    - _Requirements: 3.4, 3.6, 3.7, 3.8, 3.9, 9.1, 9.2, 9.3_

  - [ ]* 3.5 Write property test for LookupEntry serialization (Property 1)
    - **Property 1: LookupEntry serialization contains all required fields**
    - Generate random LookupEntry values (both IPv4 and IPv6), serialize to JSON, verify all seven keys present with correct types and values matching original address bytes
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 3.4, 3.7, 9.3**

  - [ ]* 3.6 Write property test for response thread count (Property 2)
    - **Property 2: get_flow_table response contains one element per thread**
    - Generate random thread configurations with varying processor_data (some nullptr, some with tables of varying entry counts), verify response `threads` array has one element per thread with correct entry counts
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 3.6, 3.8, 9.1, 9.2**

- [x] 4. Checkpoint — Control plane async get_flow_table
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Extend Python test infrastructure with processor settings
  - [x] 5.1 Add processor_name and processor_params to TestConfigGenerator
    - In `tests/fixtures/config_generator.py`, add `processor_name=None` and `processor_params=None` parameters to `generate_config()`
    - When `processor_name` is provided, include `"processor_name"` in each `pmd_threads` entry
    - When `processor_params` is provided (dict), include `"processor_params"` in each `pmd_threads` entry
    - When omitted, these fields are absent from the JSON (backward compatible)
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 5.2 Add --processor and --processor-param CLI arguments to generate_dpdk_config.py
    - Add `--processor` argument (type=str) for processor name
    - Add `--processor-param` argument (action="append", metavar="KEY=VALUE") for processor parameters
    - Parse `--processor-param` values into a dict and pass to `generate_config()`
    - _Requirements: 4.4, 4.5_

  - [x] 5.3 Update conftest.py test_config fixture
    - Extract `processor_name` and `processor_params` from `request.param` dict
    - Pass them to `TestConfigGenerator.generate_config()`
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 5.4 Add get_flow_table method to ControlClient
    - In `tests/fixtures/control_client.py`, add `def get_flow_table(self) -> Dict[str, Any]` that calls `self.send_command("get_flow_table")`
    - _Requirements: 5.1, 5.2, 5.3, 9.1_

  - [ ]* 5.5 Write property test for config generator processor settings (Property 3)
    - **Property 3: Config generator includes processor settings when provided**
    - Generate random non-None processor_name strings and processor_params dicts, verify generated config includes them in every pmd_threads entry; when None, verify keys are absent
    - Use Hypothesis with minimum 100 iterations
    - **Validates: Requirements 4.1, 4.2, 4.3**

- [x] 6. Checkpoint — Python infrastructure
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement e2e tests for five-tuple forwarding
  - [x] 7.1 Create test_five_tuple_forwarding.py with shared config and helpers
    - Create `tests/e2e/test_five_tuple_forwarding.py`
    - Define `FIVE_TUPLE_CONFIG` dict with `num_ports=1, num_threads=1, num_rx_queues=1, num_tx_queues=1, processor_name="five_tuple_forwarding", processor_params={"capacity": "1024"}`
    - Add helper to collect all flow entries from `get_flow_table` response across all threads
    - Add helper to send a packet via scapy `sendp()` on a TAP interface
    - _Requirements: 5.2, 9.1_

  - [x] 7.2 Implement test_flow_table_response_structure
    - Parametrize with `FIVE_TUPLE_CONFIG`
    - Call `get_flow_table` on a running process with FiveTupleForwardingProcessor
    - Verify `status == "success"`, `result` contains `threads` array, each element has `lcore_id` (int) and `entries` (array)
    - _Requirements: 9.1, 9.2, 9.3_

  - [x] 7.3 Implement test_single_ipv4_flow_entry
    - Parametrize with `FIVE_TUPLE_CONFIG`
    - Send a single IPv4/TCP packet with known src_ip, dst_ip, src_port, dst_port via scapy on the TAP interface
    - Call `get_flow_table` and verify an entry matching the sent packet's five-tuple exists
    - Verify `is_ipv6 == false`, `protocol == 6` (TCP)
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 7.4 Implement test_multiple_distinct_flows
    - Parametrize with `FIVE_TUPLE_CONFIG`
    - Send multiple IPv4 packets with distinct five-tuples via scapy
    - Call `get_flow_table` and verify one entry per unique five-tuple
    - Verify total entry count equals number of distinct five-tuples sent
    - _Requirements: 6.1, 6.2_

  - [x] 7.5 Implement test_duplicate_packets_single_entry
    - Parametrize with `FIVE_TUPLE_CONFIG`
    - Send at least two packets with identical five-tuples via scapy
    - Call `get_flow_table` and verify exactly one entry for that five-tuple
    - _Requirements: 7.1, 7.2_

  - [x] 7.6 Implement test_ipv6_flow_entry
    - Parametrize with `FIVE_TUPLE_CONFIG`
    - Send a single IPv6/TCP packet with known addresses and ports via scapy
    - Call `get_flow_table` and verify an entry with `is_ipv6 == true` and matching addresses/ports
    - _Requirements: 8.1_

- [x] 8. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests use RapidCheck (C++) and Hypothesis (Python) with minimum 100 iterations per property
- All 3 correctness properties from the design document are covered as property test sub-tasks
- E2E tests use scapy for packet crafting and the existing pytest fixture infrastructure
