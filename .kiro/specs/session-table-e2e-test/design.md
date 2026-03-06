# Design Document: Session Table E2E Test

## Overview

This feature adds the infrastructure and tests needed to verify session table behavior end-to-end. It spans four layers:

1. **Control plane** — a new `get_sessions` command in `CommandHandler` that iterates the `SessionTable` and serializes key/entry fields to JSON.
2. **CLI** — a new `fwdcli show sessions` cobra command (with `--json` support) that sends `get_sessions` and formats the output.
3. **Config generator** — a `session_capacity` parameter in `TestConfigGenerator.generate_config()` and a `--session-capacity` CLI flag in `generate_dpdk_config.py`.
4. **E2E tests** — pytest tests that launch the DPDK application with session tracking enabled, inject packets via scapy through TAP interfaces, and assert session table state via the `get_sessions` control plane command.

The design reuses existing patterns: `HandleGetFlowTable` for command handler iteration, `fwdcli stats` for CLI structure, `test_five_tuple_forwarding.py` for e2e test structure, and the existing `TestConfigGenerator` API for config generation.

## Architecture

```mermaid
graph TD
    subgraph "E2E Test (pytest)"
        T[test_session_table.py] -->|scapy sendp| TAP[TAP Interface dtap0]
        T -->|get_sessions| CC[ControlClient]
    end

    subgraph "DPDK Application"
        TAP -->|RX| PMD[PMD Thread]
        PMD -->|parse + insert| ST[SessionTable]
        CP[ControlPlane] -->|owns| ST
        CH[CommandHandler] -->|ForEach| ST
        CC -->|unix socket| USS[UnixSocketServer]
        USS --> CH
    end

    subgraph "CLI"
        CLI[fwdcli show sessions] -->|unix socket| USS
    end

    subgraph "Config"
        CG[TestConfigGenerator] -->|session_capacity| JSON[dpdk.json]
        JSON --> CP
    end
```

The `get_sessions` command is synchronous (unlike `get_flow_table` which requires RCU grace periods) because the `SessionTable::ForEach` iterates via `rte_hash_iterate` which is safe for concurrent reads on a lock-free hash.

## Components and Interfaces

### 1. CommandHandler — `get_sessions` command

**Files**: `control/command_handler.h`, `control/command_handler.cc`

The `CommandHandler` needs access to the `SessionTable*`. Since the `ControlPlane` owns the `SessionTable`, it will pass the pointer to `CommandHandler` after initialization (similar to how `SetRcuManager` works).

New method:
```cpp
void SetSessionTable(session::SessionTable* session_table);
```

New handler registered in constructor or after `SetSessionTable`:
```cpp
RegisterCommand("get_sessions", "session",
    [this](const json& params) { return HandleGetSessions(params); });
```

`HandleGetSessions` implementation:
- If `session_table_` is `nullptr` (capacity=0), return `{"status":"success","result":{"sessions":[]}}`.
- Otherwise, call `session_table_->ForEach(...)` with a lambda that never deletes (returns `false`), collecting each `SessionKey` + `SessionEntry` into a JSON array.
- For each entry, serialize:
  - `src_ip`, `dst_ip` — use `inet_ntop` (AF_INET or AF_INET6 based on `flags & 1`)
  - `src_port`, `dst_port`, `protocol`, `zone_id` — integer fields
  - `is_ipv6` — `bool(flags & 1)`
  - `version` — `entry->version.load(std::memory_order_relaxed)`
  - `timestamp` — `entry->timestamp.load(std::memory_order_relaxed)`

### 2. Fwdcli — `show sessions` command

**Files**: `fwdcli/cmd/sessions.go`, `fwdcli/formatter/formatter.go`

New cobra command `sessions` added as a subcommand of a new `show` parent command (or directly as `fwdcli sessions` to match the flat pattern used by existing commands like `stats`, `status`, `threads`).

Given the requirements specify `fwdcli show sessions`, we'll add a `showCmd` parent and a `sessionsCmd` child:

**`fwdcli/cmd/show.go`**:
```go
var showCmd = &cobra.Command{
    Use:   "show",
    Short: "Show various runtime information",
}

func init() {
    rootCmd.AddCommand(showCmd)
}
```

**`fwdcli/cmd/sessions.go`**:
- Sends `get_sessions` command via `client.Send("get_sessions")`
- If `--json` flag: output raw JSON via `formatter.FormatJSON`
- Otherwise: format as human-readable table via new `formatter.FormatSessions`
- On empty sessions list: print "No active sessions."
- On connection error: print to stderr, exit with `ExitConnError`

**`fwdcli/formatter/formatter.go`** — new types and formatter:
```go
type SessionInfo struct {
    SrcIP     string `json:"src_ip"`
    DstIP     string `json:"dst_ip"`
    SrcPort   int    `json:"src_port"`
    DstPort   int    `json:"dst_port"`
    Protocol  int    `json:"protocol"`
    ZoneID    int    `json:"zone_id"`
    IsIPv6    bool   `json:"is_ipv6"`
    Version   int    `json:"version"`
    Timestamp uint64 `json:"timestamp"`
}

type SessionsResult struct {
    Sessions []SessionInfo `json:"sessions"`
}

func FormatSessions(result json.RawMessage) (string, error)
```

The table format will display columns: `SRC_IP`, `DST_IP`, `SRC_PORT`, `DST_PORT`, `PROTO`, `ZONE`, `IPV6`, `VERSION`, `TIMESTAMP`.

### 3. Config Generator — `session_capacity`

**Files**: `tests/fixtures/config_generator.py`, `generate_dpdk_config.py`

**`TestConfigGenerator.generate_config()`** — add optional `session_capacity: int = 0` parameter:
- When `session_capacity > 0`: add `"session_capacity": session_capacity` to the config dict.
- When `session_capacity == 0` or not provided: omit the field entirely.

**`generate_dpdk_config.py`** — add `--session-capacity` argument:
```python
parser.add_argument("--session-capacity", type=int, default=0,
                    help="Session table capacity (0 = disabled)")
```

Pass to `generate_config(session_capacity=args.session_capacity)`.

### 4. ControlClient — `get_sessions()` helper

**File**: `tests/fixtures/control_client.py`

Add a helper method following the pattern of `get_flow_table()`:
```python
def get_sessions(self) -> Dict[str, Any]:
    """Send get_sessions command and return parsed response."""
    return self.send_command("get_sessions")
```

### 5. E2E Tests — `tests/e2e/test_session_table.py`

**Test configuration**:
```python
SESSION_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"},
    "session_capacity": 1024,
}
```

**Test class**: `TestSessionTable` with `@pytest.mark.parametrize("test_config", [SESSION_CONFIG], indirect=True)`

**Test methods**:
- `test_empty_session_table` — verify empty sessions before traffic (Req 6.1)
- `test_single_ipv4_session` — send one IPv4/TCP packet, verify session entry (Req 4.1)
- `test_multiple_distinct_sessions` — send 3 distinct five-tuples, verify 3 entries (Req 4.2)
- `test_duplicate_packets_single_session` — send same five-tuple 3x, verify 1 entry (Req 4.3)
- `test_session_protocol_field` — verify protocol=6 for TCP, protocol=17 for UDP (Req 5.1)
- `test_session_version_field` — verify version > 0 (Req 5.2)
- `test_session_timestamp_field` — verify timestamp > 0 (Req 5.3)
- `test_ipv6_session` — send IPv6/TCP packet, verify is_ipv6=true (Req 7.1)

The `conftest.py` `test_config` fixture needs to pass `session_capacity` through to `TestConfigGenerator.generate_config()`.

## Data Models

### get_sessions JSON Response

```json
{
  "status": "success",
  "result": {
    "sessions": [
      {
        "src_ip": "192.168.1.100",
        "dst_ip": "10.0.0.50",
        "src_port": 12345,
        "dst_port": 80,
        "protocol": 6,
        "zone_id": 0,
        "is_ipv6": false,
        "version": 1,
        "timestamp": 1234567890
      }
    ]
  }
}
```

### dpdk.json with session_capacity

```json
{
  "core_mask": "0x3",
  "memory_channels": 4,
  "session_capacity": 1024,
  "additional_params": [...],
  "ports": [...],
  "pmd_threads": [...]
}
```

### SessionKey → JSON field mapping

| SessionKey field | JSON field | Type | Notes |
|---|---|---|---|
| `src_ip` | `src_ip` | string | `inet_ntop` formatted |
| `dst_ip` | `dst_ip` | string | `inet_ntop` formatted |
| `src_port` | `src_port` | int | |
| `dst_port` | `dst_port` | int | |
| `zone_id` | `zone_id` | int | |
| `protocol` | `protocol` | int | 6=TCP, 17=UDP |
| `flags & 1` | `is_ipv6` | bool | |

### SessionEntry → JSON field mapping

| SessionEntry field | JSON field | Type | Notes |
|---|---|---|---|
| `version.load(relaxed)` | `version` | int | Positive after init |
| `timestamp.load(relaxed)` | `timestamp` | uint64 | TSC cycles |

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: get_sessions round-trip fidelity

*For any* set of sessions inserted into the SessionTable, calling `get_sessions` should return a JSON response with `status: "success"` and a `sessions` array where every inserted session appears with matching `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `zone_id`, and `is_ipv6` fields.

**Validates: Requirements 1.1, 1.3**

### Property 2: FormatSessions output contains all session fields

*For any* `get_sessions` result containing one or more sessions, the `FormatSessions` formatter output should contain the `src_ip`, `dst_ip`, `src_port`, `dst_port`, and `protocol` values from every session entry.

**Validates: Requirements 2.1**

### Property 3: JSON mode outputs valid parseable JSON

*For any* valid `get_sessions` result, the `FormatJSON` output should be valid JSON that, when parsed, contains the same session data as the input.

**Validates: Requirements 2.2**

### Property 4: Config generator session_capacity inclusion

*For any* non-negative integer `session_capacity`, `TestConfigGenerator.generate_config(session_capacity=n)` should include `"session_capacity": n` in the output when `n > 0`, and omit the `"session_capacity"` key entirely when `n == 0`.

**Validates: Requirements 3.2, 3.3**

### Property 5: Session count equals distinct five-tuple count

*For any* set of packets sent through the data plane, the number of session entries returned by `get_sessions` should equal the number of distinct five-tuples in the packet set.

**Validates: Requirements 4.2, 4.3**

### Property 6: Protocol field matches packet protocol

*For any* session entry created from a packet with a known IP protocol number, the `protocol` field in the session entry should equal the IP protocol number of the original packet (6 for TCP, 17 for UDP).

**Validates: Requirements 5.1**

### Property 7: Session entry metadata invariants

*For any* session entry returned by `get_sessions`, the `version` field should be a positive integer (> 0) and the `timestamp` field should be a positive integer (> 0).

**Validates: Requirements 5.2, 5.3**

## Error Handling

| Scenario | Component | Behavior |
|---|---|---|
| `session_table_` is nullptr (capacity=0) | CommandHandler | Return `{"status":"success","result":{"sessions":[]}}` |
| `inet_ntop` fails on IP address | CommandHandler | Skip entry or return error response |
| Unix socket connection fails | fwdcli | Print error to stderr, exit with code 1 |
| Server returns error response | fwdcli | Print error to stderr, exit with code 3 |
| Invalid `session_capacity` value | Config generator | Raise `ValueError` (Python) |
| DPDK process fails to start | E2E test | `pytest.fail()` with stdout/stderr |
| Control client connection timeout | E2E test | `pytest.fail()` with connection error |
| TAP interface not created | E2E test | `pytest.fail()` with interface name |

## Testing Strategy

### Unit Tests

Unit tests verify individual components in isolation:

1. **CommandHandler `get_sessions`** (`control/command_handler_test.cc`):
   - Test with nullptr session table → returns empty sessions list
   - Test command is registered with tag "session"
   - Test response JSON structure

2. **Formatter `FormatSessions`** (`fwdcli/formatter/formatter_test.go`):
   - Test formatting with multiple sessions
   - Test formatting with zero sessions → "No active sessions." message
   - Test FormatJSON passthrough

3. **Config generator** (`tests/fixtures/test_config_generator.py`):
   - Test `session_capacity=1024` → field present in output
   - Test `session_capacity=0` → field absent from output
   - Test default (not provided) → field absent

4. **CLI argument parsing** (`generate_dpdk_config.py`):
   - Test `--session-capacity 1024` maps correctly

### E2E Tests

E2E tests verify the full integration (`tests/e2e/test_session_table.py`):

| Test | Description | Validates |
|---|---|---|
| `test_empty_session_table` | No traffic → empty sessions | Req 6.1 |
| `test_single_ipv4_session` | One IPv4/TCP packet → one session entry | Req 4.1 |
| `test_multiple_distinct_sessions` | 3 distinct five-tuples → 3 entries | Req 4.2 |
| `test_duplicate_packets_single_session` | Same five-tuple 3x → 1 entry | Req 4.3 |
| `test_session_protocol_field` | TCP=6, UDP=17 | Req 5.1 |
| `test_session_version_field` | version > 0 | Req 5.2 |
| `test_session_timestamp_field` | timestamp > 0 | Req 5.3 |
| `test_ipv6_session` | IPv6/TCP → is_ipv6=true | Req 7.1 |

### Property-Based Testing

Per the user's instruction, property-based testing is **not used** for this feature. All properties are validated through concrete e2e test examples and unit tests. The correctness properties above serve as formal specifications that guide the test design, but are implemented as example-based tests rather than PBT.

### Test Configuration

The e2e tests use a shared config:
```python
SESSION_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"},
    "session_capacity": 1024,
}
```

The `conftest.py` `test_config` fixture must be updated to pass `session_capacity` through to `TestConfigGenerator.generate_config()`.
