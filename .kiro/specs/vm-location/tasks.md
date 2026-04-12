# Implementation Plan: VmLocation Table

## Overview

Implement the VmLocation table feature: new `vm_location/` module with key/value types and table alias, config parsing for 4 new fields, ProcessorContext wiring, ControlPlane ownership, and CommandHandler integration. Each task builds incrementally, ending with full wiring and inspection support.

## Tasks

- [x] 1. Create vm_location module with core types and BUILD
  - [x] 1.1 Create `vm_location/vm_location_key.h` with VmLocationKey, VmLocationKeyHash, VmLocationKeyEqual
    - Define `VmLocationKey` struct with `rxtx::IpAddress ip` and `bool is_ipv6`
    - Implement `VmLocationKeyHash` using `absl::HashOf` — hash only `v4` (4 bytes) when `is_ipv6 == false`, all 16 bytes of `v6` when `is_ipv6 == true`
    - Implement `VmLocationKeyEqual` — compare only `v4` for IPv4, `memcmp` all 16 bytes for IPv6, return false when `is_ipv6` differs
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

  - [x] 1.2 Create `vm_location/tunnel_info.h` with TunnelInfo, TunnelInfoHash, TunnelInfoEqual
    - Define `TunnelInfo` struct with `rxtx::IpAddress ip` and `bool is_ipv6`
    - Implement `TunnelInfoHash` using `absl::HashOf` — same IPv4/IPv6 pattern as VmLocationKeyHash
    - Implement `TunnelInfoEqual` — same IPv4/IPv6 pattern as VmLocationKeyEqual
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

  - [x] 1.3 Create `vm_location/vm_location_table.h` with VmLocationTable type alias
    - Define `using VmLocationTable = indirect_table::IndirectTable<VmLocationKey, TunnelInfo, VmLocationKeyHash, VmLocationKeyEqual, TunnelInfoHash, TunnelInfoEqual>`
    - _Requirements: 3.1, 3.2, 3.3_

  - [x] 1.4 Create `vm_location/BUILD` with Bazel targets
    - Add `cc_library` targets for `vm_location_key`, `tunnel_info`, `vm_location_table`
    - `vm_location_key` depends on `//rxtx:packet_metadata`, `@abseil-cpp//absl/hash`, `@abseil-cpp//absl/strings`
    - `tunnel_info` depends on `//rxtx:packet_metadata`, `@abseil-cpp//absl/hash`, `@abseil-cpp//absl/strings`
    - `vm_location_table` depends on `:vm_location_key`, `:tunnel_info`, `//indirect_table:indirect_table`
    - _Requirements: 3.1_

  - [ ]* 1.5 Write unit tests for VmLocationKey hash and equality
    - Test: two IPv4 keys with same `v4` but different `v6` padding produce same hash
    - Test: two IPv4 keys with same `v4` are equal
    - Test: two IPv6 keys with same 16 bytes are equal
    - Test: IPv4 key and IPv6 key are never equal
    - Test: two IPv6 keys differing in one byte are not equal
    - Add `cc_test` target `vm_location_key_test` in `vm_location/BUILD`
    - _Requirements: 1.2, 1.3, 1.4, 1.5, 1.6_

  - [ ]* 1.6 Write unit tests for TunnelInfo hash and equality
    - Test: two IPv4 TunnelInfo with same `v4` but different `v6` padding produce same hash
    - Test: two IPv4 TunnelInfo with same `v4` are equal
    - Test: two IPv6 TunnelInfo with same 16 bytes are equal
    - Test: IPv4 and IPv6 TunnelInfo are never equal
    - Add `cc_test` target `tunnel_info_test` in `vm_location/BUILD`
    - _Requirements: 2.2, 2.3, 2.4, 2.5_

- [x] 2. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Add VmLocation config fields and parsing
  - [x] 3.1 Add 4 `uint32_t` fields to `DpdkConfig` in `config/dpdk_config.h`
    - Add `vm_location_value_capacity`, `vm_location_value_bucket_count`, `vm_location_key_capacity`, `vm_location_key_bucket_count`, all defaulting to 0
    - _Requirements: 4.1, 4.2, 4.3, 4.4_

  - [x] 3.2 Parse 4 new JSON fields in `config/config_parser.cc`
    - Parse each field as `is_number_unsigned()`, return `InvalidArgumentError` if present but wrong type
    - Add all 4 field names to the `known_fields` set
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_

  - [ ]* 3.3 Write unit tests for VmLocation config parsing in `config/config_parser_test.cc`
    - Test: parse all 4 fields with valid values
    - Test: absent fields default to 0
    - Test: non-unsigned-integer value for each field returns InvalidArgument error
    - Test: fields are not added to `additional_params`
    - Follow existing test pattern (TestCase helper, main function)
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 5.1, 5.2, 5.3, 5.4, 5.5, 5.6_

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Wire VmLocationTable into ProcessorContext and ControlPlane
  - [x] 5.1 Add `void* vm_location_table = nullptr` to `ProcessorContext` in `processor/processor_context.h`
    - _Requirements: 7.1_

  - [x] 5.2 Add VmLocationTable ownership to `ControlPlane`
    - In `control/control_plane.h`: add `#include "vm_location/vm_location_table.h"`, add `std::unique_ptr<vm_location::VmLocationTable> vm_location_table_` member, add 4 config fields to `ControlPlane::Config`
    - _Requirements: 6.1, 6.2_

  - [x] 5.3 Implement VmLocationTable init/wire/destroy in `control/control_plane.cc`
    - In `Initialize()`: when `config_.vm_location_value_capacity > 0`, create `VmLocationTable`, call `Init()` with config and `rcu_manager_.get()`, wire into each PMD thread's `ProcessorContext::vm_location_table`, call `command_handler_->SetVmLocationTable()`
    - In `Shutdown()`: reset `vm_location_table_` after PMD threads stop but before RCU manager destruction (same ordering as `session_table_`)
    - Update `control/BUILD` to add `//vm_location:vm_location_table` dependency to `control_plane` target
    - _Requirements: 4.5, 6.1, 6.3, 6.4, 6.5, 7.2, 7.3, 8.2_

- [x] 6. Integrate VmLocationTable into CommandHandler
  - [x] 6.1 Add `SetVmLocationTable` and `HandleGetVmLocations` to `control/command_handler.h`
    - Add forward declaration or include for `vm_location::VmLocationTable`
    - Add `void SetVmLocationTable(vm_location::VmLocationTable* table)` method
    - Add `vm_location::VmLocationTable* vm_location_table_ = nullptr` member
    - Add `CommandResponse HandleGetVmLocations(const nlohmann::json& params)` private method
    - _Requirements: 8.1_

  - [x] 6.2 Implement `SetVmLocationTable` and `HandleGetVmLocations` in `control/command_handler.cc`
    - `SetVmLocationTable`: store pointer, register `"get_vm_locations"` sync command under `"vm_location"` tag (follow `SetSessionTable` pattern)
    - `HandleGetVmLocations`: iterate via `ForEachKey`, resolve each `value_id` through `slot_array().Get()`, format each entry as JSON with `dst_ip`, `tunnel_ip`, `is_ipv6`, `value_id` using `inet_ntop`
    - When `vm_location_table_` is null, return empty `vm_locations` array
    - Update `control/BUILD` to add `//vm_location:vm_location_table` dependency to `command_handler` target
    - _Requirements: 8.1, 8.3, 8.4_

- [x] 7. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- No property-based tests per user request — only example-based unit tests
- The project uses a custom `TestCase` helper pattern (not gtest) for config and command handler tests
