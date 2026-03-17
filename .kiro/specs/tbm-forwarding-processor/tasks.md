# Implementation Plan: TBM Forwarding Processor

## Overview

Add a TBM (Tree Bitmap) forwarding processor as an alternative to the existing LPM processor. The implementation mirrors `LpmForwardingProcessor` in structure (CRTP base, self-registration, burst RX/TX, PacketMetadata parsing, per-thread stats) but delegates longest-prefix-match lookups to `tbm_lookup()`. The ControlPlane owns the `FibTbm` as a direct member, initializes it alongside the existing LPM table from the same FIB file, and wires it into each PMD thread's ProcessorContext.

## Tasks

- [x] 1. Extend ProcessorContext with tbm_table field
  - [x] 1.1 Add `void* tbm_table = nullptr` field to `ProcessorContext` in `processor/processor_context.h`
    - Add the field after the existing `lpm_table` field
    - Must not affect existing fields (`stats`, `session_table`, `processor_data`, `lpm_table`, `pmd_job_runner`)
    - _Requirements: 1.1, 1.2_

- [x] 2. Implement TBM FIB loader
  - [x] 2.1 Add `LoadFibFileToTbm` declaration to `fib/fib_loader.h`
    - Add `extern "C" { #include "tbm/tbmlib.h" }` include with C linkage wrapper
    - Declare `absl::Status LoadFibFileToTbm(const std::string& file_path, FibTbm* tbm, uint32_t* rules_loaded = nullptr)` in the `fib` namespace
    - _Requirements: 4.1_
  - [x] 2.2 Implement `LoadFibFileToTbm` in `fib/fib_loader.cc`
    - Mirror the existing `LoadFibFile` structure: open file, read line pairs, parse IPv4 via `inet_pton`, validate CIDR 0–32
    - Return `InvalidArgumentError` if `tbm` is null (Req 4.8)
    - Return `NotFoundError` if file cannot be opened (Req 4.3)
    - Return `InvalidArgumentError` with line number for invalid IP or CIDR (Req 4.4, 4.5)
    - Construct `FibCidr{.ip = ip_host, .cidr = cidr}` and call `tbm_insert(tbm, cidr_val, 0)`
    - Return `InternalError` if `tbm_insert()` returns non-null fault (Req 4.6)
    - Store count in `rules_loaded` if non-null (Req 4.7)
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8_
  - [x] 2.3 Add `//tbm:tbmlib` dependency to `fib_loader` target in `fib/BUILD`
    - _Requirements: 13.3_
  - [x] 2.4 Write unit tests for `LoadFibFileToTbm` in `fib/fib_loader_test.cc`
    - Test null `tbm` pointer returns `InvalidArgumentError`
    - Test non-existent file returns `NotFoundError`
    - Test valid FIB file: load entries, verify `rules_loaded` count, iterate with `tbm_iterate` to confirm entries
    - Test invalid IPv4 address returns `InvalidArgumentError`
    - Test CIDR outside 0–32 returns `InvalidArgumentError`
    - Update `fib_loader_test` target in `fib/BUILD` to add `//tbm:tbmlib` dep
    - _Requirements: 4.2, 4.3, 4.4, 4.5, 4.7, 4.8_

- [x] 3. Checkpoint - Ensure FIB loader builds and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement TbmForwardingProcessor
  - [x] 4.1 Create `processor/tbm_forwarding_processor.h`
    - Define `TbmForwardingProcessor` class inheriting from `PacketProcessorBase<TbmForwardingProcessor>` (CRTP)
    - Include `extern "C" { #include "tbm/tbmlib.h" }` for `FibTbm*` type
    - Constructor takes `PmdThreadConfig&` and optional `PacketStats*`
    - Declare `check_impl`, `process_impl`, `ExportProcessorData`, `CheckParams` — mirror `LpmForwardingProcessor` signatures exactly
    - Private members: `kBatchSize = 64`, `PacketStats* stats_`, `FibTbm* tbm_table_`
    - No `RegisterControlCommands` method (Req 12.3)
    - _Requirements: 5.1, 6.1, 6.2, 7.1, 8.1, 11.1, 11.2, 12.1, 12.2, 12.3_
  - [x] 4.2 Create `processor/tbm_forwarding_processor.cc`
    - `check_impl`: return `InvalidArgumentError` if `tx_queues` empty, `OkStatus` otherwise (Req 6.1, 6.2)
    - `ExportProcessorData`: cast `ctx.tbm_table` to `FibTbm*` and cache in `tbm_table_` only if `ctx.tbm_table` is non-null (since `tbm_table_` is a direct member of ControlPlane, its address always exists — ControlPlane only sets `ctx.tbm_table` when FIB was actually loaded); do NOT touch `session_table`, `processor_data`, or `lpm_table` (Req 12.2)
    - `CheckParams`: return `OkStatus` for empty map, `InvalidArgumentError` for non-empty (Req 11.1, 11.2)
    - `process_impl`: for each RX queue, burst RX → `PacketMetadata::Parse` each packet → if parse OK and `tbm_table_ != nullptr` and not IPv6, call `tbm_lookup(&next_hop, tbm_table_, ntohl(meta.dst_ip.v4))` → record stats if `stats_` non-null → `rte_eth_tx_burst` on `tx_queues[0]` → free unsent mbufs → `batch.Release()`
    - Add `REGISTER_PROCESSOR("tbm_forwarding", TbmForwardingProcessor)` at bottom
    - _Requirements: 5.1, 5.2, 6.1, 6.2, 7.1, 7.2, 8.1, 8.2, 8.3, 8.4, 8.5, 9.1, 9.2, 10.1, 10.2, 11.1, 11.2, 12.1, 12.2, 12.3_
  - [x] 4.3 Add `tbm_forwarding_processor` target to `processor/BUILD`
    - Use `alwayslink = True` to preserve `REGISTER_PROCESSOR` static initializer (Req 13.2)
    - Add deps: `:packet_processor_base`, `:packet_stats`, `:processor_context`, `:processor_registry`, `//config:dpdk_config`, `//rxtx:batch`, `//rxtx:packet`, `//rxtx:packet_metadata`, `//rxtx:packet_metadata_impl`, `//tbm:tbmlib`, `//:dpdk_lib`, abseil status/strings/flat_hash_map
    - _Requirements: 13.1, 13.2_
  - [x] 4.4 Write unit tests for TbmForwardingProcessor in `processor/tbm_forwarding_processor_test.cc`
    - Test `ProcessorRegistry::Lookup("tbm_forwarding")` returns OK with non-null launcher, checker, param_checker (Req 5.1)
    - Test both `"lpm_forwarding"` and `"tbm_forwarding"` are registered without conflict (Req 14.1)
    - Test `check_impl` with empty `tx_queues` returns `InvalidArgumentError` (Req 6.1)
    - Test `check_impl` with non-empty `tx_queues` returns `OkStatus` (Req 6.2)
    - Test `CheckParams` with empty map returns `OkStatus` (Req 11.1)
    - Test `CheckParams` with non-empty map returns `InvalidArgumentError` (Req 11.2)
    - Test `ExportProcessorData` does not modify `session_table` or `processor_data` (Req 1.2, 12.2)
    - Add `tbm_forwarding_processor_test` target to `processor/BUILD` with deps on `:tbm_forwarding_processor`, `:processor_registry`, `//config:dpdk_config`, `//tbm:tbmlib`, abseil
    - _Requirements: 5.1, 6.1, 6.2, 11.1, 11.2, 12.2, 14.1_

- [x] 5. Checkpoint - Ensure processor builds and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Integrate TBM FIB into ControlPlane
  - [x] 6.1 Add TBM members and include to `control/control_plane.h`
    - Add `extern "C" { #include "tbm/tbmlib.h" }` include
    - Add private members: `FibTbm tbm_table_{};`, `bool tbm_initialized_ = false;`, `uint32_t tbm_rules_loaded_ = 0;`
    - _Requirements: 2.1_
  - [x] 6.2 Add TBM initialization to `ControlPlane::Initialize` in `control/control_plane.cc`
    - After existing LPM table creation block, add TBM block: zero-init `tbm_table_ = {}`, call `tbm_init(&tbm_table_, 1048576)`, set `tbm_initialized_ = true`
    - Call `fib::LoadFibFileToTbm(config_.fib_file, &tbm_table_, &tbm_rules_loaded_)`
    - On error: call `tbm_free(&tbm_table_)`, set `tbm_initialized_ = false`, return error (Req 2.3)
    - Wire `tbm_table` into each PMD thread's ProcessorContext via `thread_manager_` only when `tbm_initialized_` is true — this ensures `ctx.tbm_table` stays null when no FIB was loaded, so `ExportProcessorData` can null-check it (Req 3.1)
    - Skip if `fib_file` is empty (Req 2.4)
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 3.1, 3.2, 14.2_
  - [x] 6.3 Add TBM cleanup to `ControlPlane::Shutdown` in `control/control_plane.cc`
    - After PMD threads stop and after LPM table destruction, call `tbm_free(&tbm_table_)` if `tbm_initialized_`, set `tbm_initialized_ = false`
    - _Requirements: 2.5_
  - [x] 6.4 Add `//tbm:tbmlib` dependency to `control_plane` target in `control/BUILD`
    - _Requirements: 13.3_

- [x] 7. Wire TBM processor into main binary
  - [x] 7.1 Add `//processor:tbm_forwarding_processor` dependency to `main` target in root `BUILD`
    - _Requirements: 13.4, 14.1, 14.3_

- [x] 8. Final checkpoint - Ensure full build succeeds
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- The TBM processor mirrors `LpmForwardingProcessor` exactly in structure — use it as the reference implementation
- The `tbmlib` header must always be included with `extern "C"` wrapper since it is a C library used from C++
- `FibTbm` is owned as a direct member of ControlPlane (not heap-allocated), zero-initialized with `= {}` before `tbm_init()`
- Both LPM and TBM tables are created from the same `fib_file`, enabling mixed processor configurations
