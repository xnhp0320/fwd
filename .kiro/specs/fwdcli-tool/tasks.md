# Implementation Plan: fwdcli-tool

## Overview

Build a Go CLI tool (`fwdcli`) that communicates with the DPDK forwarding application's control plane over a Unix domain socket. The implementation proceeds bottom-up: Bazel setup → client package → formatter package → cobra commands → monitor mode → final wiring. All code lives under `fwdcli/` at the project root and is built with `bazel build //fwdcli`.

## Tasks

- [x] 1. Set up Bazel Go toolchain and project skeleton
  - [x] 1.1 Add rules_go and gazelle to MODULE.bazel
    - Add `bazel_dep(name = "rules_go", version = "0.53.0")` and `bazel_dep(name = "gazelle", version = "0.42.0")`
    - Add `go_deps` extension with `go_deps.from_file(go_mod = "//fwdcli:go.mod")` and `use_repo(go_deps, "com_github_spf13_cobra")`
    - _Requirements: 1.3_

  - [x] 1.2 Create fwdcli directory structure and go.mod
    - Create `fwdcli/go.mod` with module name and Go version
    - Run `go mod tidy` (or equivalent) to generate `go.sum` with cobra dependency
    - Create directories: `fwdcli/client/`, `fwdcli/cmd/`, `fwdcli/formatter/`
    - _Requirements: 1.1, 1.3_

  - [x] 1.3 Create BUILD files for all packages
    - Create `fwdcli/BUILD` with `go_binary` (embed fwdcli_lib) and `go_library` for main.go, including `x_defs` for version injection
    - Create `fwdcli/client/BUILD` with `go_library` and `go_test`
    - Create `fwdcli/cmd/BUILD` with `go_library` depending on `//fwdcli/client` and `//fwdcli/formatter` and cobra
    - Create `fwdcli/formatter/BUILD` with `go_library` and `go_test`
    - _Requirements: 1.3_

  - [x] 1.4 Create minimal main.go
    - Create `fwdcli/main.go` that imports `fwdcli/cmd` and calls `cmd.Execute()`
    - _Requirements: 1.1_

- [x] 2. Implement client package (Unix socket communication)
  - [x] 2.1 Implement client.go with Client struct, New, Connect, Close, and Send methods
    - `New(socketPath, timeout)` stores config without connecting
    - `Connect()` checks socket file existence, dials `unix` network, stores `net.Conn`
    - `Close()` closes the connection
    - `Send(command)` writes `{"command":"<name>"}\n`, sets read deadline, reads until newline, parses JSON into `Response` struct
    - `Response` struct uses `json.RawMessage` for `Result` field
    - `IsSuccess()` method checks `Status == "success"`
    - Default timeout of 5 seconds
    - _Requirements: 2.1, 2.2, 2.4, 2.5, 2.6, 2.7_

  - [ ]* 2.2 Write property test: Command serialization format (Property 1)
    - **Property 1: Command serialization format**
    - For any valid command name, verify `Send()` writes exactly `{"command":"<name>"}\n` to the socket and the output is valid JSON
    - Use a mock Unix socket server in test to capture written bytes
    - **Validates: Requirements 2.4**

  - [ ]* 2.3 Write property test: Response deserialization round-trip (Property 2)
    - **Property 2: Response deserialization round-trip**
    - For any valid JSON response with status "success" or "error", parsing and re-serializing preserves status, result, and error fields
    - Use `testing/quick` to generate arbitrary response payloads
    - **Validates: Requirements 2.5**

  - [x] 2.4 Write unit tests for client edge cases
    - Test connecting to a non-existent socket returns error
    - Test timeout when server doesn't respond within deadline
    - Test shutdown command treats EOF as success (Requirement 6.3)
    - Test connecting to a valid mock socket succeeds
    - _Requirements: 2.6, 2.7, 6.3_

- [x] 3. Implement formatter package (output rendering)
  - [x] 3.1 Implement formatter.go with all format functions
    - Define Go structs: `StatusResult`, `ThreadInfo`, `ThreadsResult`, `ThreadStats`, `TotalStats`, `StatsResult`
    - `FormatStatus(json.RawMessage)` — unmarshal to `StatusResult`, render main_lcore, num_pmd_threads, uptime_seconds
    - `FormatThreads(json.RawMessage)` — unmarshal to `ThreadsResult`, render each lcore_id
    - `FormatStats(json.RawMessage)` — unmarshal to `StatsResult`, render tabular per-thread packets/bytes + total row
    - `FormatStatsMonitor(json.RawMessage, time.Time)` — prepend ANSI clear `\033[2J\033[H`, add timestamp header, then stats table
    - `FormatJSON(json.RawMessage)` — `json.MarshalIndent` for pretty-printed JSON
    - _Requirements: 3.2, 4.2, 5.2, 7.5, 7.9, 8.1_

  - [ ]* 3.2 Write property test: Status formatter completeness (Property 3)
    - **Property 3: Status formatter completeness**
    - For any `StatusResult` with non-negative values, `FormatStatus()` output contains string representations of all three fields
    - **Validates: Requirements 3.2**

  - [ ]* 3.3 Write property test: Threads formatter completeness (Property 4)
    - **Property 4: Threads formatter completeness**
    - For any `ThreadsResult` with arbitrary lcore_id values, `FormatThreads()` output contains every lcore_id as a string
    - **Validates: Requirements 4.2**

  - [ ]* 3.4 Write property test: Stats formatter completeness (Property 5)
    - **Property 5: Stats formatter completeness**
    - For any `StatsResult` with arbitrary values, `FormatStats()` output contains string representations of every per-thread value and both totals
    - **Validates: Requirements 5.2**

  - [ ]* 3.5 Write property test: Monitor output format (Property 9)
    - **Property 9: Monitor output format**
    - For any `StatsResult` and timestamp, `FormatStatsMonitor()` output begins with ANSI clear-screen codes and contains the timestamp string
    - **Validates: Requirements 7.5, 7.9**

  - [ ]* 3.6 Write property test: JSON output mode passthrough (Property 10)
    - **Property 10: JSON output mode passthrough**
    - For any valid JSON response, `FormatJSON()` output is valid JSON semantically equivalent to the input
    - **Validates: Requirements 8.1**

  - [ ]* 3.7 Write property test: JSON monitor mode line format (Property 11)
    - **Property 11: JSON monitor mode line format**
    - For any sequence of stats responses in JSON monitor mode, each output line is a valid self-contained JSON object
    - **Validates: Requirements 8.3**

- [x] 4. Checkpoint - Verify client and formatter packages build and test
  - Ensure `bazel test //fwdcli/client/...` and `bazel test //fwdcli/formatter/...` pass. Ask the user if questions arise.

- [x] 5. Implement cobra command tree (cmd package)
  - [x] 5.1 Implement root.go with global flags and Execute()
    - Define root cobra command with `--socket` (default `/tmp/dpdk_control.sock`), `--json` (bool), `--version` (bool) persistent flags
    - Define `version` variable (injected via `x_defs` at build time)
    - `Execute()` calls `rootCmd.Execute()` and maps errors to exit codes
    - Define exit code constants: `ExitSuccess=0`, `ExitConnError=1`, `ExitUsageError=2`, `ExitServerError=3`
    - _Requirements: 2.2, 2.3, 8.2, 9.1, 9.3, 9.4, 10.1, 10.2, 10.3, 10.4_

  - [x] 5.2 Implement status.go subcommand
    - Register `status` subcommand with root
    - Create client, connect, send `"status"`, format response (human-readable or JSON based on `--json` flag)
    - Handle error responses (exit code 3) and connection errors (exit code 1)
    - _Requirements: 3.1, 3.2, 3.3_

  - [x] 5.3 Implement threads.go subcommand
    - Register `threads` subcommand with root
    - Create client, connect, send `"get_threads"`, format response
    - Handle error responses and connection errors
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 5.4 Implement stats.go subcommand
    - Register `stats` subcommand with root
    - Create client, connect, send `"get_stats"`, format response
    - Handle error responses and connection errors
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 5.5 Implement shutdown.go subcommand
    - Register `shutdown` subcommand with root
    - Create client, connect, send `"shutdown"`, handle success and EOF-as-success case
    - Print confirmation on success, handle error responses
    - _Requirements: 6.1, 6.2, 6.3, 6.4_

  - [ ]* 5.6 Write property test: Error response handling (Property 6)
    - **Property 6: Error response handling**
    - For any `Response` with status "error" and non-empty error message, verify the error message appears on stderr and exit code is 3
    - **Validates: Requirements 3.3, 4.3, 5.3, 6.4, 10.4**

  - [ ]* 5.7 Write property test: Exit code mapping (Property 7)
    - **Property 7: Exit code mapping**
    - For any command execution outcome, verify exit code is deterministic: 0 success, 1 connection error, 2 usage error, 3 server error
    - **Validates: Requirements 10.1, 10.2, 10.3, 10.4**

  - [ ]* 5.8 Write property test: Socket path override (Property 12)
    - **Property 12: Socket path override**
    - For any valid filesystem path string provided via `--socket`, verify the client connects to exactly that path
    - **Validates: Requirements 2.3**

- [x] 6. Checkpoint - Verify all subcommands build
  - Ensure `bazel build //fwdcli` and `bazel test //fwdcli/...` pass. Ask the user if questions arise.

- [x] 7. Implement monitor mode
  - [x] 7.1 Implement monitor.go subcommand
    - Register `monitor` subcommand with root, add `--interval` flag (int, default 1)
    - Validate interval in `PreRunE`: reject non-positive values with exit code 2
    - Create client, connect once, reuse connection across polling cycles
    - Use `signal.NotifyContext` for SIGINT/SIGTERM cancellation
    - Use `time.NewTicker` for polling loop
    - Each tick: send `"get_stats"`, render with `FormatStatsMonitor` (or JSON line if `--json`)
    - On signal: close client, exit 0
    - On server error: print to stderr, exit 1
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8, 7.9, 8.3_

  - [ ]* 7.2 Write property test: Interval validation (Property 8)
    - **Property 8: Interval validation**
    - For any integer value, verify it is accepted iff positive; non-positive integers and non-numeric strings are rejected with exit code 2
    - **Validates: Requirements 7.3, 7.4**

- [x] 8. Final checkpoint - Ensure all tests pass
  - Ensure `bazel test //fwdcli/...` passes. Ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Property tests use Go's `testing/quick` package (stdlib) with minimum 100 iterations
- All tests run via `bazel test //fwdcli/...`
- Mock Unix socket servers (`net.Listen("unix", ...)`) are used for client tests
- Checkpoints ensure incremental validation at package boundaries
