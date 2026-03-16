# Implementation Plan: LPM Forwarding Processor

## Overview

Add an LPM (Longest Prefix Match) forwarding processor to the DPDK packet processing pipeline. The implementation touches DpdkConfig, ConfigParser, ProcessorContext, a new FIB loader stub, ControlPlane LPM table lifecycle, and the LpmForwardingProcessor class itself. All code is C++ with Bazel build, following existing patterns from SimpleForwardingProcessor and FiveTupleForwardingProcessor.

## Tasks

- [x] 1. Extend DpdkConfig and ConfigParser with fib_file field
  - [x] 1.1 Add `std::string fib_file` field to `DpdkConfig` in `config/dpdk_config.h`
    - Default to empty string (no FIB)
    - _Requirements: 1.1, 1.2_
  - [x] 1.2 Add fib_file parsing to `ConfigParser::ParseString` in `config/config_parser.cc`
    - Parse optional `"fib_file"` string field from JSON
    - Return `InvalidArgumentError` if field is present but not a string
    - Add `"fib_file"` to the `known_fields` set so it does not leak into `additional_params`
    - _Requirements: 1.1, 1.2, 1.3_
  - [ ]* 1.3 Add unit tests for fib_file parsing in `config/config_parser_test.cc`
    - Test: JSON with `"fib_file": "/path/to/fib.txt"` → `config.fib_file == "/path/to/fib.txt"`
    - Test: JSON without `"fib_file"` → `config.fib_file == ""`
    - Test: JSON with `"fib_file": 123` → `InvalidArgumentError`
    - Test: `"fib_file"` does not appear in `additional_params`
    - Follow existing `TestCase()` pattern with `main()` (no gtest)
    - _Requirements: 1.1, 1.2, 1.3_

- [x] 2. Extend ProcessorContext with lpm_table pointer
  - [x] 2.1 Add `void* lpm_table = nullptr` field to `ProcessorContext` in `processor/processor_context.h`
    - Place after existing `processor_data` field
    - _Requirements: 4.1_

- [x] 3. Checkpoint - Ensure config and context changes compile
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Create FIB loader stub
  - [x] 4.1 Create `fib/fib_loader.h` with `fib::LoadFibFile` declaration
    - Signature: `absl::Status LoadFibFile(const std::string& file_path, struct rte_lpm* lpm)`
    - Forward-declare `struct rte_lpm` to avoid DPDK header dependency in the header
    - _Requirements: 5.1, 5.3_
  - [x] 4.2 Create `fib/fib_loader.cc` with stub implementation
    - Return `absl::OkStatus()` unconditionally
    - _Requirements: 5.2_
  - [x] 4.3 Create `fib/BUILD` with `fib_loader` cc_library and `fib_loader_test` cc_test targets
    - `fib_loader` depends on `@abseil-cpp//absl/status`
    - _Requirements: 5.1_
  - [ ]* 4.4 Create `fib/fib_loader_test.cc` unit test
    - Test: `LoadFibFile("any_path", nullptr)` returns `OkStatus`
    - Follow existing `TestCase()` + `main()` pattern
    - _Requirements: 5.2_

- [x] 5. Extend ControlPlane to create, wire, and destroy rte_lpm table
  - [x] 5.1 Add `fib_file` field to `ControlPlane::Config` and `struct rte_lpm* lpm_table_ = nullptr` private member in `control/control_plane.h`
    - Include `fib/fib_loader.h` header
    - _Requirements: 2.1, 2.4_
  - [x] 5.2 Add LPM table creation in `ControlPlane::Initialize` in `control/control_plane.cc`
    - If `config_.fib_file` is non-empty: create `rte_lpm` with `max_rules = 1048576`, `number_tbl8s = 65536`
    - Call `fib::LoadFibFile` after creation; clean up on failure
    - Return error if `rte_lpm_create` fails
    - Wire `lpm_table_` into each PMD thread's `ProcessorContext::lpm_table`
    - _Requirements: 2.1, 2.2, 2.3, 3.1, 3.2_
  - [x] 5.3 Add LPM table destruction in `ControlPlane::Shutdown` in `control/control_plane.cc`
    - Call `rte_lpm_free(lpm_table_)` after PMD threads stop, before SessionTable destruction
    - _Requirements: 2.5_
  - [x] 5.4 Update `control/BUILD` to add `//fib:fib_loader` dependency to `control_plane` target
    - _Requirements: 2.1_

- [x] 6. Checkpoint - Ensure ControlPlane changes compile
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement LpmForwardingProcessor
  - [x] 7.1 Create `processor/lpm_forwarding_processor.h`
    - CRTP class inheriting `PacketProcessorBase<LpmForwardingProcessor>`
    - Constructor takes `PmdThreadConfig&` and optional `PacketStats*`
    - Declare `check_impl`, `process_impl`, `ExportProcessorData`, `CheckParams`
    - Private members: `kBatchSize = 64`, `stats_`, `lpm_table_`
    - No `RegisterControlCommands` method
    - _Requirements: 6.1, 7.1, 7.2, 12.1, 12.2, 13.3_
  - [x] 7.2 Create `processor/lpm_forwarding_processor.cc`
    - `check_impl`: return `InvalidArgumentError` if `tx_queues` empty, else `OkStatus`
    - `ExportProcessorData`: cache `ctx.lpm_table` as `rte_lpm*`; do NOT touch `session_table` or `processor_data`
    - `CheckParams`: return `InvalidArgumentError` if params map is non-empty, else `OkStatus`
    - `process_impl`: for each RX queue, rx_burst → parse each packet with `PacketMetadata::Parse` → if IPv4 and `lpm_table_ != nullptr`, call `rte_lpm_lookup` → record stats → tx_burst on `tx_queues[0]` → free unsent mbufs → `batch.Release()`
    - Skip LPM lookup on parse failure, IPv6, or null `lpm_table_`
    - Register with `REGISTER_PROCESSOR("lpm_forwarding", LpmForwardingProcessor)`
    - _Requirements: 6.1, 6.2, 7.1, 7.2, 8.1, 8.2, 9.1, 9.2, 9.3, 9.4, 9.5, 10.1, 10.2, 11.1, 11.2, 12.1, 12.2, 13.1, 13.2, 13.3_
  - [x] 7.3 Add `lpm_forwarding_processor` cc_library target to `processor/BUILD`
    - Use `alwayslink = True` for self-registration
    - Dependencies: `packet_processor_base`, `packet_stats`, `processor_context`, `processor_registry`, `//config:dpdk_config`, `//rxtx:batch`, `//rxtx:packet_metadata`, `//rxtx:packet_metadata_impl`, `//:dpdk_lib`, `@abseil-cpp//absl/container:flat_hash_map`, `@abseil-cpp//absl/status`, `@abseil-cpp//absl/strings`
    - _Requirements: 6.1_

- [ ] 8. Add unit tests for LpmForwardingProcessor
  - [ ]* 8.1 Create `processor/lpm_forwarding_processor_test.cc`
    - Test registration: `ProcessorRegistry::Lookup("lpm_forwarding")` returns OK with non-null launcher, checker, param_checker
    - Test `check_impl`: empty `tx_queues` → `InvalidArgumentError`; non-empty → `OkStatus`
    - Test `CheckParams`: empty map → `OkStatus`; non-empty map → `InvalidArgumentError`
    - Test `ExportProcessorData`: when `ctx.lpm_table` is set, processor caches it; when null, remains null; does NOT write `ctx.session_table` or `ctx.processor_data`
    - Follow existing `TestCase()` + `main()` pattern (no gtest for processor tests)
    - _Requirements: 6.1, 6.2, 7.1, 7.2, 9.4, 12.1, 12.2, 13.1, 13.2, 13.3_
  - [ ]* 8.2 Add `lpm_forwarding_processor_test` cc_test target to `processor/BUILD`
    - Dependencies: `lpm_forwarding_processor`, `processor_registry`, `//config:dpdk_config`, `@abseil-cpp//absl/container:flat_hash_map`, `@abseil-cpp//absl/status`
    - _Requirements: 6.1_

- [x] 9. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- The design uses C++ throughout; no language selection needed
- `kBatchSize = 64`, `lpm_conf.max_rules = 1048576`, `lpm_conf.number_tbl8s = 65536` per user-specified values
- No property-based tests; unit tests only
- No FastLookupTable, SessionTable, or control-plane command dependencies
