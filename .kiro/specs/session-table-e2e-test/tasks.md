# Implementation Plan: Session Table E2E Test

## Overview

Implement the `get_sessions` control plane command, `fwdcli show sessions` CLI command, config generator `session_capacity` support, and e2e tests that verify session table behavior by injecting packets via scapy and querying session state. Changes are ordered: C++ command handler first, then Go CLI, then Python infrastructure, then e2e tests.

## Tasks

- [x] 1. Implement `get_sessions` command in CommandHandler
  - [x] 1.1 Add `SetSessionTable` and `HandleGetSessions` to CommandHandler
    - In `control/command_handler.h`, add `#include "session/session_table.h"` and a `session::SessionTable* session_table_ = nullptr` member
    - Add `void SetSessionTable(session::SessionTable* session_table)` public method
    - Add `CommandResponse HandleGetSessions(const nlohmann::json& params)` private method
    - In `control/command_handler.cc`, implement `SetSessionTable` to store the pointer
    - Register the command: `RegisterCommand("get_sessions", "session", [this](const json& params) { return HandleGetSessions(params); });` — register in `SetSessionTable` (only when pointer is non-null) or unconditionally in the constructor
    - Implement `HandleGetSessions`: if `session_table_` is nullptr, return success with empty sessions array; otherwise call `session_table_->ForEach(...)` with a lambda that collects each `SessionKey` + `SessionEntry*` into a JSON array using `inet_ntop` for IP addresses (AF_INET or AF_INET6 based on `flags & 1`), and returns `false` (never delete)
    - Each session JSON object: `src_ip`, `dst_ip` (string via inet_ntop), `src_port`, `dst_port`, `protocol`, `zone_id` (int), `is_ipv6` (bool from `flags & 1`), `version` (from `entry->version.load(relaxed)`), `timestamp` (from `entry->timestamp.load(relaxed)`)
    - In `control/BUILD`, add `"//session:session_table"` to the `command_handler` library deps
    - _Requirements: 1.1, 1.2, 1.3, 1.4_

  - [x] 1.2 Wire `SetSessionTable` in ControlPlane
    - In `control/control_plane.cc`, after the `SessionTable` is initialized, call `command_handler_->SetSessionTable(&session_table_)` (or however the control plane owns the session table)
    - _Requirements: 1.1_

  - [ ]* 1.3 Write unit tests for `HandleGetSessions`
    - In `control/command_handler_test.cc`, add tests:
      - Test with nullptr session table → returns `{"status":"success","result":{"sessions":[]}}` 
      - Test that `get_sessions` command is registered with tag `"session"` via `GetCommandsByTag("session")`
      - Test response JSON structure matches expected format
    - _Requirements: 1.1, 1.2, 1.3, 1.4_

- [x] 2. Checkpoint — C++ get_sessions command
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Implement `fwdcli show sessions` command
  - [x] 3.1 Add `SessionInfo`, `SessionsResult`, and `FormatSessions` to formatter
    - In `fwdcli/formatter/formatter.go`, add `SessionInfo` struct with JSON tags: `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `zone_id`, `is_ipv6`, `version`, `timestamp`
    - Add `SessionsResult` struct with `Sessions []SessionInfo`
    - Implement `FormatSessions(result json.RawMessage) (string, error)` — unmarshal to `SessionsResult`, if empty print "No active sessions.", otherwise render a table with columns: SRC_IP, DST_IP, SRC_PORT, DST_PORT, PROTO, ZONE, IPV6, VERSION, TIMESTAMP
    - Update `fwdcli/formatter/BUILD` if needed
    - _Requirements: 2.1, 2.3_

  - [x] 3.2 Add `show` parent command and `sessions` child command
    - Create `fwdcli/cmd/show.go` with a `showCmd` cobra command (`Use: "show"`, `Short: "Show various runtime information"`) and `init()` that adds it to `rootCmd`
    - Create `fwdcli/cmd/sessions.go` with a `sessionsCmd` cobra command (`Use: "sessions"`) added to `showCmd`
    - `sessionsCmd.RunE`: create client, connect, send `"get_sessions"`, if `--json` flag use `formatter.FormatJSON`, otherwise use `formatter.FormatSessions`; on connection error print to stderr and exit with `ExitConnError`; on empty sessions print "No active sessions."
    - Update `fwdcli/cmd/BUILD` to include `show.go` and `sessions.go` in srcs
    - _Requirements: 2.1, 2.2, 2.3, 2.4_

  - [ ]* 3.3 Write unit tests for `FormatSessions`
    - In `fwdcli/formatter/formatter_test.go`, add tests:
      - Test formatting with multiple sessions → output contains all field values
      - Test formatting with zero sessions → output contains "No active sessions."
      - Test `FormatJSON` passthrough for sessions result
    - _Requirements: 2.1, 2.2, 2.3_

- [x] 4. Checkpoint — fwdcli show sessions command
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Extend config generator with `session_capacity`
  - [x] 5.1 Add `session_capacity` parameter to `TestConfigGenerator.generate_config()`
    - In `tests/fixtures/config_generator.py`, add `session_capacity: int = 0` parameter to `generate_config()`
    - When `session_capacity > 0`, add `"session_capacity": session_capacity` to the config dict
    - When `session_capacity == 0` or not provided, omit the key entirely
    - _Requirements: 3.1, 3.2, 3.3_

  - [x] 5.2 Add `--session-capacity` CLI argument to `generate_dpdk_config.py`
    - Add `parser.add_argument("--session-capacity", type=int, default=0, help="Session table capacity (0 = disabled)")`
    - Pass `session_capacity=args.session_capacity` to `TestConfigGenerator.generate_config()`
    - _Requirements: 3.4_

  - [x] 5.3 Update `conftest.py` to pass `session_capacity` through to config generator
    - In `tests/conftest.py`, extract `session_capacity` from `request.param` dict (default 0)
    - Pass `session_capacity=session_capacity` to `TestConfigGenerator.generate_config()`
    - _Requirements: 3.1, 3.2_

  - [ ]* 5.4 Write unit tests for config generator `session_capacity`
    - In `tests/fixtures/test_config_generator.py`, add tests:
      - Test `session_capacity=1024` → `"session_capacity"` key present with value 1024
      - Test `session_capacity=0` → `"session_capacity"` key absent
      - Test default (not provided) → `"session_capacity"` key absent
    - _Requirements: 3.1, 3.2, 3.3_

- [x] 6. Checkpoint — Config generator session_capacity
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Add `get_sessions()` helper to ControlClient
  - [x] 7.1 Add `get_sessions()` method to ControlClient
    - In `tests/fixtures/control_client.py`, add `def get_sessions(self) -> Dict[str, Any]` that calls `self.send_command("get_sessions")`
    - Follow the existing pattern of `get_flow_table()`
    - _Requirements: 8.1_

- [x] 8. Implement e2e tests for session table
  - [x] 8.1 Create `test_session_table.py` with shared config and helpers
    - Create `tests/e2e/test_session_table.py`
    - Define `SESSION_CONFIG` dict: `num_ports=1, num_threads=1, num_rx_queues=1, num_tx_queues=1, processor_name="five_tuple_forwarding", processor_params={"capacity": "1024"}, session_capacity=1024`
    - Add `bring_interfaces_up()` helper (reuse pattern from `test_five_tuple_forwarding.py`)
    - Add `send_packet()` helper for IPv4/IPv6 TCP/UDP packets via scapy
    - Add `collect_sessions()` helper to extract sessions list from `get_sessions` response
    - Define `TestSessionTable` class with `@pytest.mark.parametrize("test_config", [SESSION_CONFIG], indirect=True)`
    - _Requirements: 4.1, 4.2, 4.3, 5.1, 5.2, 5.3, 6.1, 7.1_

  - [x] 8.2 Implement `test_empty_session_table`
    - Verify that before any traffic, `get_sessions` returns success with an empty sessions list
    - _Requirements: 6.1_

  - [x] 8.3 Implement `test_single_ipv4_session`
    - Send one IPv4/TCP packet with known src_ip, dst_ip, src_port, dst_port
    - Call `get_sessions` and verify a session entry exists with matching five-tuple fields
    - Verify `is_ipv6 == false`, `protocol == 6`
    - _Requirements: 4.1_

  - [x] 8.4 Implement `test_multiple_distinct_sessions`
    - Send 3 packets with distinct five-tuples
    - Call `get_sessions` and verify 3 separate session entries exist
    - _Requirements: 4.2_

  - [x] 8.5 Implement `test_duplicate_packets_single_session`
    - Send the same five-tuple 3 times
    - Call `get_sessions` and verify exactly 1 session entry for that five-tuple
    - _Requirements: 4.3_

  - [x] 8.6 Implement `test_session_protocol_field`
    - Send a TCP packet (protocol=6) and a UDP packet (protocol=17)
    - Verify each session entry's `protocol` field matches the sent packet's IP protocol number
    - _Requirements: 5.1_

  - [x] 8.7 Implement `test_session_version_field`
    - Send a packet, retrieve the session entry, verify `version > 0`
    - _Requirements: 5.2_

  - [x] 8.8 Implement `test_session_timestamp_field`
    - Send a packet, retrieve the session entry, verify `timestamp > 0`
    - _Requirements: 5.3_

  - [x] 8.9 Implement `test_ipv6_session`
    - Send a single IPv6/TCP packet
    - Verify session entry has `is_ipv6 == true` and matching `src_port`, `dst_port`, `protocol` fields
    - _Requirements: 7.1_

- [-] 9. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation at layer boundaries
- No property-based testing — all correctness properties are validated through concrete e2e and unit tests
- E2E tests use scapy for packet crafting and the existing pytest fixture infrastructure
- The `send_packet` helper should support both TCP and UDP for protocol field testing (Req 5.1)
