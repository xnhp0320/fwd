# Implementation Plan: Five-Tuple Forwarding

## Overview

Implement the FiveTupleForwarding processor, per-processor config parameters, command tag system, and CLI commands subcommand. Tasks are ordered so each step builds on the previous: config layer first, then processor registration and implementation, then control plane, then CLI.

## Tasks

- [x] 1. Extend config layer with processor_params
  - [x] 1.1 Add processor_params field to PmdThreadConfig
    - Add `absl::flat_hash_map<std::string, std::string> processor_params` to `PmdThreadConfig` in `config/dpdk_config.h`
    - Add `#include "absl/container/flat_hash_map.h"` to the header
    - Update `config/BUILD` deps to include `@abseil-cpp//absl/container:flat_hash_map`
    - _Requirements: 4.3_

  - [x] 1.2 Parse processor_params in ConfigParser
    - In `config/config_parser.cc`, after parsing the `"processor"` field in each pmd_thread entry, parse an optional `"processor_params"` JSON object
    - Each key-value pair stored as string→string in `PmdThreadConfig::processor_params`
    - If the field is absent, the map remains empty
    - _Requirements: 4.1, 4.2_

  - [ ]* 1.3 Write property test for processor_params round-trip (Property 3)
    - **Property 3: processor_params round-trip through JSON**
    - Generate random string-to-string maps, serialize as `"processor_params"` JSON object inside a pmd_thread entry, parse with ConfigParser, verify the resulting `processor_params` field equals the original map
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 4.1**

  - [x] 1.4 Add unit tests for processor_params parsing
    - Test absent `processor_params` field produces empty map
    - Test `processor_params` with multiple key-value pairs
    - Add tests to `config/config_parser_test.cc`
    - _Requirements: 4.1, 4.2_

- [x] 2. Extend ProcessorRegistry with ParamCheckFn
  - [x] 2.1 Add ParamCheckFn to ProcessorEntry and MakeProcessorEntry
    - In `processor/processor_registry.h`, add `ParamCheckFn` type alias: `std::function<absl::Status(const absl::flat_hash_map<std::string, std::string>&)>`
    - Add `ParamCheckFn param_checker` field to `ProcessorEntry`
    - Update `MakeProcessorEntry<T>()` to wire `T::CheckParams` as the `param_checker`
    - Update `processor/BUILD` deps to include `@abseil-cpp//absl/container:flat_hash_map`
    - _Requirements: 5.1_

  - [x] 2.2 Add CheckParams to SimpleForwardingProcessor
    - Add `static absl::Status CheckParams(const absl::flat_hash_map<std::string, std::string>& params)` to `SimpleForwardingProcessor`
    - Return OK for empty map; return InvalidArgument listing the unrecognized key for any non-empty map
    - _Requirements: 5.4, 5.5_

  - [ ]* 2.3 Write property test for empty params accepted by all processors (Property 6)
    - **Property 6: Empty parameter map is accepted by all processors**
    - For each registered processor in ProcessorRegistry, call `param_checker` with an empty map, verify OK
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 5.5**

  - [ ]* 2.4 Write property test for unrecognized keys rejected (Property 5)
    - **Property 5: Unrecognized processor parameter keys are rejected**
    - Generate random string keys not in the recognized set for SimpleForwardingProcessor, call `param_checker` with `{key: "1"}`, verify InvalidArgument with error containing the key name
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 5.3**

- [x] 3. Integrate param validation into ConfigValidator
  - [x] 3.1 Call param_checker in ConfigValidator
    - In `config/config_validator.cc`, during PMD thread validation, after checking lcore/queue assignments, look up the processor name in `ProcessorRegistry` and call `entry->param_checker(pmd_config.processor_params)`
    - If the param check fails, return the error
    - Add `processor/processor_registry.h` to includes and `//processor:processor_registry` to `config/BUILD` deps
    - _Requirements: 5.6_

  - [x] 3.2 Add unit tests for param validation in ConfigValidator
    - Test that valid params pass validation
    - Test that unrecognized params fail validation with InvalidArgument
    - Add tests to `config/config_validator_test.cc`
    - _Requirements: 5.6_

- [x] 4. Checkpoint — Config and registry changes
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement FiveTupleForwardingProcessor
  - [x] 5.1 Create five_tuple_forwarding_processor.h and .cc
    - Create `processor/five_tuple_forwarding_processor.h` with the class declaration inheriting from `PacketProcessorBase<FiveTupleForwardingProcessor>`
    - Create `processor/five_tuple_forwarding_processor.cc` with `check_impl`, `process_impl`, `CheckParams`, and `REGISTER_PROCESSOR("five_tuple_forwarding", FiveTupleForwardingProcessor)`
    - `check_impl`: return InvalidArgument if `tx_queues` is empty, OK otherwise
    - `process_impl`: burst RX → parse PacketMetadata → Find/Insert in FastLookupTable → tx_burst to tx_queues[0] → free untransmitted mbufs
    - Constructor reads `config.processor_params["capacity"]` or defaults to 65536
    - `CheckParams`: accept `"capacity"` key (must be positive integer), reject unrecognized keys, accept empty map
    - _Requirements: 1.1, 1.2, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.2, 5.2, 5.3, 5.5, 6.1, 6.2, 6.3_

  - [x] 5.2 Add BUILD target for five_tuple_forwarding_processor
    - Add `cc_library` target in `processor/BUILD` with `alwayslink = True`
    - Deps: `:packet_processor_base`, `:packet_stats`, `:processor_registry`, `//config:dpdk_config`, `//rxtx:fast_lookup_table`, `//rxtx:packet_metadata`, `//rxtx:batch`, `//:dpdk_lib`, `@abseil-cpp//absl/status`, `@abseil-cpp//absl/strings`, `@abseil-cpp//absl/container:flat_hash_map`
    - _Requirements: 1.1_

  - [ ]* 5.3 Write property test for check_impl accepts non-empty tx_queues (Property 2)
    - **Property 2: check_impl accepts all non-empty tx_queues**
    - Generate random non-empty vectors of QueueAssignment, call `check_impl`, verify OK
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 3.2**

  - [ ]* 5.4 Write property test for capacity validation (Property 4)
    - **Property 4: Capacity validation accepts valid positive integers and rejects all others**
    - Generate random strings, call `CheckParams` with `{"capacity": value}`, verify OK iff value is a positive integer string
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 5.2, 6.3**

  - [ ]* 5.5 Write property test for configured capacity (Property 7)
    - **Property 7: Configured capacity matches table capacity**
    - Generate random positive integers, construct FiveTupleForwardingProcessor with `processor_params = {"capacity": "<value>"}`, verify internal table capacity matches
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 6.1**

  - [ ]* 5.6 Write property test for insert-on-miss (Property 1)
    - **Property 1: Insert-on-miss populates the flow table**
    - Generate random PacketMetadata, call Find (returns nullptr), call Insert with five-tuple fields, call Find again, verify non-null and key fields match
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 2.3**

  - [x] 5.7 Add unit tests for FiveTupleForwardingProcessor
    - Test registration: `ProcessorRegistry::Lookup("five_tuple_forwarding")` returns valid entry with non-null launcher, checker, and param_checker
    - Test `check_impl` edge case: empty tx_queues returns InvalidArgument
    - Test default capacity: constructing without `"capacity"` param yields table capacity 65536
    - Add test file `processor/five_tuple_forwarding_processor_test.cc` and BUILD target
    - _Requirements: 1.1, 1.2, 3.1, 6.2_

- [x] 6. Checkpoint — Processor implementation
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement command tag system and new commands
  - [x] 7.1 Refactor CommandHandler to use a command registry with tags
    - In `control/command_handler.h`, add `CommandEntry` struct with `tag` and `handler` fields
    - Replace the if-else dispatch in `ExecuteCommand` with a `flat_hash_map<string, CommandEntry> commands_` registry
    - Add `RegisterCommand(name, tag, handler)` private method
    - Register existing commands (shutdown, status, get_threads, get_stats) with tag `"common"` in the constructor
    - Add `GetCommandsByTag(tag)` and `GetAllCommands()` methods
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [x] 7.2 Implement list_commands command
    - Register `"list_commands"` with tag `"common"`
    - Without `"tag"` param: return all commands with their tags as `{"commands": [{"name": ..., "tag": ...}, ...]}`
    - With `"tag"` param: return only commands matching the tag
    - _Requirements: 10.1, 10.2, 10.3_

  - [x] 7.3 Implement get_flow_table command
    - Register `"get_flow_table"` with tag `"five_tuple_forwarding"`
    - If active processor is FiveTupleForwarding, return `{"entry_count": N}`
    - Otherwise return error with status `"not_supported"`
    - This requires a mechanism to query the active processor type — add a processor type accessor or callback to CommandHandler
    - _Requirements: 8.1, 8.2, 8.3_

  - [ ]* 7.4 Write property test for tag filtering (Property 8)
    - **Property 8: Tag filtering returns exactly the matching commands**
    - Generate random tag strings, register commands with various tags, call `GetCommandsByTag`, verify the result is exactly the set of commands with matching tags
    - Use RapidCheck with minimum 100 iterations
    - **Validates: Requirements 7.3, 10.3**

  - [x] 7.5 Add unit tests for command tag system
    - Test existing commands have tag "common"
    - Test get_flow_table has tag "five_tuple_forwarding"
    - Test list_commands has tag "common"
    - Test get_flow_table returns "not_supported" when processor is not FiveTupleForwarding
    - Test list_commands unfiltered returns all commands
    - Test list_commands filtered by tag returns correct subset
    - Add tests to `control/command_handler_test.cc`
    - _Requirements: 7.1, 7.2, 7.3, 8.1, 8.3, 10.1, 10.2, 10.3_

- [x] 8. Checkpoint — Control plane changes
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Implement CLI commands subcommand
  - [x] 9.1 Add SendWithParams to CLI client
    - In `fwdcli/client/client.go`, add `SendWithParams(command string, params map[string]string) (*Response, error)` that builds `{"command":"<name>","params":{...}}`
    - Refactor existing `Send` to delegate to `SendWithParams` with nil params
    - _Requirements: 9.2, 9.3_

  - [x] 9.2 Add FormatCommands to formatter
    - In `fwdcli/formatter/formatter.go`, add `FormatCommands(result json.RawMessage) (string, error)` that renders a table with columns `COMMAND` and `TAG`
    - _Requirements: 9.4_

  - [ ]* 9.3 Write property test for FormatCommands (Property 9)
    - **Property 9: Command list formatter includes all command names and tags**
    - Generate random lists of `{name, tag}` entries, call FormatCommands, verify output contains every name and tag
    - Use Go `testing/quick` or `pgregory.net/rapid` with minimum 100 iterations
    - **Validates: Requirements 9.4**

  - [x] 9.4 Create commands.go subcommand
    - Create `fwdcli/cmd/commands.go` with cobra command `commands`
    - Add `--tag` string flag for optional filtering
    - Send `list_commands` request (with optional `tag` param via `SendWithParams`)
    - Format output as table via `FormatCommands`, or raw JSON with `--json`
    - Register the command in `init()` via `rootCmd.AddCommand(commandsCmd)`
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

  - [x] 9.5 Update fwdcli BUILD files
    - Add `commands.go` to `fwdcli/cmd/BUILD` srcs
    - _Requirements: 9.1_

  - [x] 9.6 Add unit tests for CLI commands subcommand
    - Test SendWithParams builds correct JSON payload
    - Test FormatCommands output contains command names and tags
    - Test `--json` flag outputs raw JSON
    - _Requirements: 9.2, 9.3, 9.4, 9.5_

- [x] 10. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests use RapidCheck (C++) and Go rapid/testing-quick (Go) with minimum 100 iterations per property
- All 9 correctness properties from the design document are covered as property test sub-tasks
