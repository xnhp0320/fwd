# Implementation Plan: Packet Processor

## Overview

Implement a CRTP-based packet processor abstraction with a registry for config-driven selection, a simple forwarding processor, config schema extension, and PMD thread integration. The implementation proceeds bottom-up: core abstractions first, then the concrete processor, config parsing, and finally PMD thread wiring.

## Tasks

- [x] 1. Create processor directory with PacketProcessorBase and ProcessorRegistry
  - [x] 1.1 Create `processor/packet_processor_base.h` with the CRTP base class template
    - Define `PacketProcessorBase<Derived>` with `Check()` and `Process()` delegating to `check_impl` / `process_impl`
    - Store `PmdThreadConfig` by value, expose via `config()` accessor
    - _Requirements: 1.1, 1.2, 1.4, 2.1, 2.3_

  - [x] 1.2 Create `processor/processor_registry.h` and `processor/processor_registry.cc` with the ProcessorRegistry singleton
    - Define `LauncherFn`, `CheckFn`, `ProcessorEntry` types
    - Implement `Register()`, `Lookup()`, `RegisteredNames()`, `kDefaultProcessorName`
    - Implement `MakeProcessorEntry<T>()` helper template and `REGISTER_PROCESSOR` macro
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

  - [x] 1.3 Create `processor/BUILD` with Bazel targets for `packet_processor_base` and `processor_registry`
    - Add `cc_library` targets with appropriate deps (abseil, dpdk_config)
    - _Requirements: 3.1_

  - [ ]* 1.4 Write property test: unknown processor name yields error containing the name
    - **Property 3: Unknown processor name yields error containing the name**
    - Generate random non-registered strings, verify `Lookup()` returns `NotFoundError` with the name in the message
    - **Validates: Requirements 3.3**

  - [ ]* 1.5 Write unit tests for ProcessorRegistry
    - Verify `Register` + `Lookup` round-trip, `RegisteredNames()` listing, duplicate registration behavior
    - _Requirements: 3.1, 3.2, 3.4_

- [x] 2. Implement SimpleForwardingProcessor
  - [x] 2.1 Create `processor/simple_forwarding_processor.h` and `processor/simple_forwarding_processor.cc`
    - Inherit from `PacketProcessorBase<SimpleForwardingProcessor>`
    - Implement `check_impl`: require exactly 1 TX queue, accept any RX count, return descriptive error
    - Implement `process_impl`: `rte_eth_rx_burst` from each RX queue, `rte_eth_tx_burst` to the single TX queue, free untransmitted mbufs
    - Self-register via `REGISTER_PROCESSOR("simple_forwarding", SimpleForwardingProcessor)` in the .cc file
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [x] 2.2 Add `simple_forwarding_processor` target to `processor/BUILD`
    - Depend on `packet_processor_base`, `processor_registry`, `//rxtx:batch`, dpdk_lib
    - _Requirements: 6.1_

  - [ ]* 2.3 Write property test: check succeeds iff exactly 1 TX queue
    - **Property 1: SimpleForwarding check succeeds iff exactly 1 TX queue**
    - Generate random RX/TX queue vectors, assert `check_impl` returns Ok iff `tx.size() == 1`, and error message contains actual TX count when not 1
    - **Validates: Requirements 1.1, 6.2, 6.3**

  - [ ]* 2.4 Write property test: processor config preservation
    - **Property 2: Processor config preservation**
    - Generate random `PmdThreadConfig`, construct `SimpleForwardingProcessor`, verify `config()` fields match original
    - **Validates: Requirements 1.4**

  - [ ]* 2.5 Write unit tests for SimpleForwardingProcessor check_impl
    - Test specific cases: 0 TX queues, 1 TX queue, 3 TX queues, 0 RX queues with 1 TX queue
    - _Requirements: 6.2, 6.3_

- [x] 3. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Extend PmdThreadConfig and ConfigParser for processor name
  - [x] 4.1 Add `processor_name` field to `PmdThreadConfig` in `config/dpdk_config.h`
    - Add `std::string processor_name` field (empty string = use default)
    - _Requirements: 4.1_

  - [x] 4.2 Update `config/config_parser.cc` to parse the optional `"processor"` JSON field
    - Parse `"processor"` string field from each `pmd_threads` entry into `PmdThreadConfig::processor_name`
    - Leave empty when field is omitted
    - _Requirements: 4.2, 4.3_

  - [x] 4.3 Update `config/config_printer.cc` to output the `processor_name` field when non-empty
    - _Requirements: 4.2_

  - [ ]* 4.4 Write property test: processor name JSON round-trip
    - **Property 4: Processor name JSON round-trip**
    - Generate random printable ASCII strings, embed as `"processor"` in PMD thread JSON, parse via `ConfigParser`, verify round-trip
    - **Validates: Requirements 4.2**

  - [ ]* 4.5 Write unit tests for config parser processor field
    - Test: JSON with `"processor"` field parsed correctly, JSON without `"processor"` field yields empty string
    - _Requirements: 4.2, 4.3_

- [x] 5. Integrate processor into PMD thread startup and hot loop
  - [x] 5.1 Update `PMDThreadManager::LaunchThreads()` in `config/pmd_thread_manager.cc`
    - Look up processor by name (or default) from `ProcessorRegistry`
    - Call `checker()` before `rte_eal_remote_launch`; abort with descriptive error on failure
    - _Requirements: 5.1, 5.2, 5.3, 7.1_

  - [x] 5.2 Update `PmdThread::Run()` in `config/pmd_thread.cc`
    - Replace the stub loop with registry lookup + `launcher()` call
    - The launcher runs the monomorphized hot loop (no virtual dispatch)
    - _Requirements: 2.1, 2.2, 7.2, 7.3_

  - [x] 5.3 Update `config/BUILD` to add `//processor:processor_registry` and `//processor:simple_forwarding_processor` deps to `pmd_thread` and `pmd_thread_manager` targets
    - _Requirements: 7.1_

  - [ ]* 5.4 Write property test: failed processor check aborts thread launch with descriptive error
    - **Property 5: Failed processor check aborts thread launch with descriptive error**
    - Generate `PmdThreadConfig` with TX count != 1 and processor "simple_forwarding", verify `LaunchThreads` returns non-Ok status containing lcore ID and processor name
    - **Validates: Requirements 5.2, 5.3**

- [x] 6. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Property tests use RapidCheck (already in MODULE.bazel)
- The `process_impl` untransmitted-packet-free property (Property 6) requires DPDK mock infrastructure and is deferred to e2e testing
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
