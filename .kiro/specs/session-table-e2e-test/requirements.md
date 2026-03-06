# Requirements Document

## Introduction

This feature adds a CLI command to inspect session table contents, extends the config generator to support session table parameters, and provides end-to-end tests that verify session entries are created when packets are forwarded through the DPDK application. The tests use scapy to generate packets and the `fwdcli show sessions` command (via the control plane unix socket) to verify session table state.

## Glossary

- **Session_Table**: The `session::SessionTable` backed by `rte_hash` and `rte_mempool`, storing per-flow session state keyed by five-tuple plus zone ID.
- **Session_Entry**: A cache-line-aligned struct (`session::SessionEntry`) containing version and timestamp atomics, stored in the Session_Table.
- **Session_Key**: A 44-byte flat struct (`session::SessionKey`) containing src/dst IP, src/dst port, zone_id, protocol, and flags.
- **Command_Handler**: The `dpdk_config::CommandHandler` class that processes JSON commands received over the control plane unix socket.
- **Control_Client**: The Python `ControlClient` fixture that sends JSON commands to the control plane unix socket and parses responses.
- **Fwdcli**: The Go-based CLI tool (`fwdcli`) that communicates with the DPDK control plane over a unix domain socket using cobra commands.
- **Config_Generator**: The Python `TestConfigGenerator` class and `generate_dpdk_config.py` CLI tool that produce `dpdk.json` configuration files.
- **TAP_Interface**: A Linux TAP virtual network device created by the `net_tap` DPDK PMD, used for injecting and capturing packets in e2e tests.
- **E2E_Test**: An end-to-end test that launches the full DPDK application, sends packets via scapy through TAP interfaces, and verifies behavior via control plane commands.

## Requirements

### Requirement 1: get_sessions Control Plane Command

**User Story:** As a developer, I want to query the session table contents via the control plane, so that I can inspect active sessions for debugging and testing.

#### Acceptance Criteria

1. WHEN a `get_sessions` command is received, THE Command_Handler SHALL iterate all entries in the Session_Table and return a JSON response containing each session's key fields (src_ip, dst_ip, src_port, dst_port, protocol, zone_id, is_ipv6) and entry fields (version, timestamp).
2. WHEN a `get_sessions` command is received and the Session_Table is not configured (session_capacity is 0), THE Command_Handler SHALL return a success response with an empty sessions list.
3. THE `get_sessions` command response SHALL use the JSON structure: `{"status": "success", "result": {"sessions": [...]}}` where each session object contains the key and entry fields.
4. WHEN a `get_sessions` command is received, THE Command_Handler SHALL register the command with the tag `session`.

### Requirement 2: Fwdcli "show sessions" Command

**User Story:** As a developer, I want a CLI command to display session table contents in a human-readable format, so that I can quickly inspect sessions without parsing raw JSON.

#### Acceptance Criteria

1. WHEN `fwdcli show sessions` is executed, THE Fwdcli SHALL send a `get_sessions` command to the control plane unix socket and display the session entries in a human-readable table format.
2. WHEN `fwdcli show sessions --json` is executed, THE Fwdcli SHALL output the raw JSON response from the `get_sessions` command.
3. WHEN the `get_sessions` response contains zero sessions, THE Fwdcli SHALL display a message indicating no active sessions.
4. IF the control plane connection fails, THEN THE Fwdcli SHALL print an error message to stderr and exit with a non-zero exit code.

### Requirement 3: Config Generator Session Table Support

**User Story:** As a developer, I want to specify session table capacity in the config generator, so that I can generate configurations with session tracking enabled for testing.

#### Acceptance Criteria

1. THE Config_Generator SHALL accept a `session_capacity` parameter that specifies the maximum number of concurrent sessions.
2. WHEN `session_capacity` is provided and greater than 0, THE Config_Generator SHALL include a `"session_capacity"` field in the generated JSON configuration.
3. WHEN `session_capacity` is not provided or is 0, THE Config_Generator SHALL omit the `"session_capacity"` field from the generated JSON configuration.
4. THE `generate_dpdk_config.py` CLI tool SHALL accept a `--session-capacity` argument that maps to the `session_capacity` config parameter.

### Requirement 4: E2E Test — Session Creation on Packet Forwarding

**User Story:** As a developer, I want an e2e test that verifies session entries are created when packets are forwarded, so that I can confirm the session table integration works end-to-end.

#### Acceptance Criteria

1. WHEN a single IPv4/TCP packet is sent through a TAP_Interface, THE E2E_Test SHALL verify that a session entry exists in the Session_Table with matching src_ip, dst_ip, src_port, dst_port, and protocol fields by querying the `get_sessions` command.
2. WHEN multiple packets with distinct five-tuples are sent, THE E2E_Test SHALL verify that each distinct five-tuple produces a separate session entry.
3. WHEN multiple packets with the same five-tuple are sent, THE E2E_Test SHALL verify that only one session entry exists for that five-tuple.

### Requirement 5: E2E Test — Session Entry Field Validation

**User Story:** As a developer, I want e2e tests that validate session entry fields, so that I can confirm the session key and entry metadata are correctly populated.

#### Acceptance Criteria

1. WHEN a session entry is retrieved via `get_sessions`, THE E2E_Test SHALL verify that the `protocol` field matches the IP protocol number of the sent packet (6 for TCP, 17 for UDP).
2. WHEN a session entry is retrieved via `get_sessions`, THE E2E_Test SHALL verify that the `version` field is a positive integer (indicating the entry was initialized).
3. WHEN a session entry is retrieved via `get_sessions`, THE E2E_Test SHALL verify that the `timestamp` field is a positive integer (indicating the entry timestamp was set).

### Requirement 6: E2E Test — Empty Session Table

**User Story:** As a developer, I want an e2e test that verifies the session table is empty before any traffic is sent, so that I can confirm there are no spurious session entries.

#### Acceptance Criteria

1. WHEN the DPDK application starts with session tracking enabled and no traffic has been sent, THE E2E_Test SHALL verify that the `get_sessions` command returns an empty sessions list.

### Requirement 7: E2E Test — IPv6 Session Entry

**User Story:** As a developer, I want an e2e test that verifies IPv6 packets create session entries, so that I can confirm dual-stack session tracking works.

#### Acceptance Criteria

1. WHEN a single IPv6/TCP packet is sent through a TAP_Interface, THE E2E_Test SHALL verify that a session entry exists with `is_ipv6` set to true and matching src_port, dst_port, and protocol fields.

### Requirement 8: Control_Client get_sessions Helper

**User Story:** As a developer, I want a `get_sessions()` helper method on the Control_Client fixture, so that e2e tests can query session table contents with a single method call.

#### Acceptance Criteria

1. THE Control_Client SHALL provide a `get_sessions()` method that sends a `get_sessions` command and returns the parsed JSON response.
