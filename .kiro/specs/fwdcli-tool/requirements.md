# Requirements Document

## Introduction

fwdcli is a standalone command-line tool for interacting with the DPDK forwarding application's control plane. It communicates over a Unix domain socket using the existing newline-delimited JSON protocol. The tool is implemented in Go to produce a single static binary with minimal dependencies, making it easy to deploy alongside the DPDK application without pulling in heavy C++ libraries (Boost, abseil, DPDK) that are unnecessary for a simple socket client.

### Technology Decision: Go over C++

Go is recommended over C++ for the following reasons:
- Go produces statically linked binaries by default — no shared library dependencies at runtime
- The CLI only needs Unix socket I/O and JSON handling, both of which are in Go's standard library
- Go has mature CLI frameworks (e.g., cobra) for subcommand routing, flag parsing, and help generation
- A C++ CLI would require linking Boost.Asio and nlohmann/json at minimum, adding build complexity for no benefit
- Go's goroutines make the monitor mode (periodic polling with signal handling) straightforward

### Build System: Bazel with rules_go

The project already uses Bazel as its build system. The Go CLI will be built using `rules_go` to keep everything in a single build graph:
- `rules_go` provides `go_binary`, `go_library`, and `go_test` rules for Bazel
- Go dependencies (e.g., cobra) are managed via `gazelle` and `go_repository` in MODULE.bazel
- The fwdcli binary is built with `bazel build //fwdcli` alongside the existing C++ targets
- Cross-compilation for linux/amd64 and linux/arm64 is handled via Bazel platform transitions rather than raw `GOOS`/`GOARCH`
- Version string injection uses Bazel's `x_defs` (equivalent to Go linker `-ldflags -X`)

## Glossary

- **Fwdcli**: The Go-based command-line tool that communicates with the DPDK forwarding application's control plane
- **Control_Plane**: The Unix domain socket server running inside the DPDK forwarding application that accepts JSON commands
- **Socket_Path**: The filesystem path to the Unix domain socket (default: `/tmp/dpdk_control.sock`)
- **Monitor_Mode**: A long-running mode where Fwdcli repeatedly queries statistics at a configurable interval until interrupted by a signal
- **JSON_Protocol**: The newline-delimited JSON protocol used for communication: client sends `{"command":"<name>"}\n`, server responds with `{"status":"success","result":{...}}\n` or `{"status":"error","error":"..."}\n`
- **Polling_Interval**: The time in seconds between successive stats queries in Monitor_Mode (configurable via `--interval` flag)

## Requirements

### Requirement 1: Single Binary Distribution

**User Story:** As an operator, I want fwdcli to be a single static binary with no runtime dependencies, so that I can deploy it by copying one file to any Linux host.

#### Acceptance Criteria

1. THE Fwdcli SHALL be implemented in Go and compile to a single statically linked binary
2. THE Fwdcli SHALL have zero runtime dependencies beyond the Linux kernel
3. THE Fwdcli SHALL be buildable via `bazel build //fwdcli` using rules_go
4. THE Fwdcli SHALL support cross-compilation for linux/amd64 and linux/arm64 targets via Bazel platform transitions

### Requirement 2: Unix Socket Communication

**User Story:** As an operator, I want fwdcli to connect to the DPDK application's control plane over a Unix domain socket, so that I can issue commands and receive responses.

#### Acceptance Criteria

1. THE Fwdcli SHALL connect to the Control_Plane via a Unix domain socket at the Socket_Path
2. THE Fwdcli SHALL use `/tmp/dpdk_control.sock` as the default Socket_Path
3. WHEN the `--socket` flag is provided, THE Fwdcli SHALL use the specified path as the Socket_Path
4. THE Fwdcli SHALL send commands using the JSON_Protocol (newline-delimited JSON)
5. THE Fwdcli SHALL read responses until a newline delimiter is received
6. IF the socket file does not exist, THEN THE Fwdcli SHALL exit with a non-zero exit code and print an error message indicating the socket was not found
7. IF the Control_Plane does not respond within 5 seconds, THEN THE Fwdcli SHALL exit with a non-zero exit code and print a timeout error message

### Requirement 3: Status Command

**User Story:** As an operator, I want to query the status of the running DPDK application, so that I can verify it is operational and see basic information.

#### Acceptance Criteria

1. WHEN the `status` subcommand is invoked, THE Fwdcli SHALL send `{"command":"status"}` to the Control_Plane
2. WHEN a successful status response is received, THE Fwdcli SHALL display the main_lcore, num_pmd_threads, and uptime_seconds fields in a human-readable format
3. IF the Control_Plane returns an error response, THEN THE Fwdcli SHALL print the error message to stderr and exit with a non-zero exit code

### Requirement 4: Get Threads Command

**User Story:** As an operator, I want to list the PMD threads running in the DPDK application, so that I can see which lcores are active.

#### Acceptance Criteria

1. WHEN the `threads` subcommand is invoked, THE Fwdcli SHALL send `{"command":"get_threads"}` to the Control_Plane
2. WHEN a successful get_threads response is received, THE Fwdcli SHALL display each thread's lcore_id in a human-readable format
3. IF the Control_Plane returns an error response, THEN THE Fwdcli SHALL print the error message to stderr and exit with a non-zero exit code

### Requirement 5: Get Stats Command

**User Story:** As an operator, I want to view per-thread and total packet/byte statistics, so that I can monitor forwarding performance.

#### Acceptance Criteria

1. WHEN the `stats` subcommand is invoked, THE Fwdcli SHALL send `{"command":"get_stats"}` to the Control_Plane
2. WHEN a successful get_stats response is received, THE Fwdcli SHALL display per-thread packets and bytes, and the total packets and bytes, in a human-readable tabular format
3. IF the Control_Plane returns an error response, THEN THE Fwdcli SHALL print the error message to stderr and exit with a non-zero exit code

### Requirement 6: Shutdown Command

**User Story:** As an operator, I want to gracefully shut down the DPDK application from the command line, so that I can stop the process without sending raw signals.

#### Acceptance Criteria

1. WHEN the `shutdown` subcommand is invoked, THE Fwdcli SHALL send `{"command":"shutdown"}` to the Control_Plane
2. WHEN a successful shutdown response is received, THE Fwdcli SHALL print a confirmation message and exit with exit code 0
3. IF the Control_Plane closes the connection before responding, THEN THE Fwdcli SHALL treat the shutdown as successful and exit with exit code 0
4. IF the Control_Plane returns an error response, THEN THE Fwdcli SHALL print the error message to stderr and exit with a non-zero exit code

### Requirement 7: Monitor Mode

**User Story:** As an operator, I want to continuously monitor forwarding statistics at a configurable interval, so that I can observe performance trends in real time.

#### Acceptance Criteria

1. WHEN the `monitor` subcommand is invoked, THE Fwdcli SHALL enter Monitor_Mode and repeatedly query statistics from the Control_Plane
2. THE Fwdcli SHALL use a default Polling_Interval of 1 second
3. WHEN the `--interval` flag is provided with a positive integer value, THE Fwdcli SHALL use that value as the Polling_Interval in seconds
4. IF the `--interval` flag is provided with a non-positive or non-numeric value, THEN THE Fwdcli SHALL print an error message and exit with a non-zero exit code
5. WHILE in Monitor_Mode, THE Fwdcli SHALL display updated statistics after each polling cycle, including a timestamp for each sample
6. WHILE in Monitor_Mode, THE Fwdcli SHALL reuse the same Unix socket connection across polling cycles
7. WHEN a SIGINT or SIGTERM signal is received during Monitor_Mode, THE Fwdcli SHALL stop polling, close the socket connection, and exit with exit code 0
8. IF the Control_Plane becomes unreachable during Monitor_Mode, THEN THE Fwdcli SHALL print an error message to stderr and exit with a non-zero exit code
9. WHILE in Monitor_Mode, THE Fwdcli SHALL clear the terminal and redraw the stats table on each polling cycle to provide a dashboard-like experience

### Requirement 8: JSON Output Mode

**User Story:** As an operator, I want to get raw JSON output from any command, so that I can pipe fwdcli output into other tools like jq for scripting and automation.

#### Acceptance Criteria

1. WHEN the `--json` flag is provided with any subcommand, THE Fwdcli SHALL output the raw JSON response from the Control_Plane to stdout
2. WHEN the `--json` flag is not provided, THE Fwdcli SHALL output human-readable formatted text
3. WHILE in Monitor_Mode with the `--json` flag, THE Fwdcli SHALL output one JSON object per line for each polling cycle

### Requirement 9: Help and Version Information

**User Story:** As an operator, I want to see usage information and the tool version, so that I can understand available commands and verify which version is deployed.

#### Acceptance Criteria

1. WHEN the `--help` flag is provided, THE Fwdcli SHALL display usage information listing all subcommands and global flags
2. WHEN a subcommand is invoked with `--help`, THE Fwdcli SHALL display usage information specific to that subcommand
3. WHEN the `--version` flag is provided, THE Fwdcli SHALL display the version string
4. THE Fwdcli SHALL embed the version string at build time via Bazel `x_defs` (Go linker flags)

### Requirement 10: Exit Code Conventions

**User Story:** As an operator, I want consistent exit codes from fwdcli, so that I can use it reliably in shell scripts and automation.

#### Acceptance Criteria

1. WHEN a command completes successfully, THE Fwdcli SHALL exit with exit code 0
2. WHEN a connection error occurs, THE Fwdcli SHALL exit with exit code 1
3. WHEN an invalid command or flag is provided, THE Fwdcli SHALL exit with exit code 2
4. WHEN the Control_Plane returns an error response, THE Fwdcli SHALL exit with exit code 3
