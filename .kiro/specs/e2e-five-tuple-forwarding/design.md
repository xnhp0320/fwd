# Design Document: E2E Five-Tuple Forwarding

## Overview

This feature bridges the gap between the existing `FiveTupleForwardingProcessor` (which inserts flow entries on the data plane) and the control plane's ability to read those entries back for verification. It extends the `get_flow_table` command from returning a simple entry count to returning fully serialized flow table entries, enables test configuration of processor selection, and adds e2e tests that send crafted packets and verify the resulting flow table state.

The changes span three layers:
1. **C++ data plane**: Add `void* processor_data` to `ProcessorContext`, promote the context to a `PmdThread` member, change the `LauncherFn` signature to non-const reference, and have `FiveTupleForwardingProcessor` store its `FastLookupTable*` in the context.
2. **C++ control plane**: Rewrite `HandleGetFlowTable` to iterate threads via `PMDThreadManager`, pause each table with `SetModifiable(false)`, use `RcuManager::CallAfterGracePeriod` to wait for PMD threads to reach a quiescent state, then read entries, serialize to JSON, and restore with `SetModifiable(true)`.
3. **Python test infrastructure**: Extend `TestConfigGenerator`, `generate_dpdk_config.py`, `conftest.py`, and `ControlClient` to support processor selection and flow table queries, then add e2e tests for IPv4, IPv6, duplicate, and multi-flow scenarios.

## Architecture

```mermaid
graph TD
    subgraph "Data Plane (PMD Thread)"
        FTF["FiveTupleForwardingProcessor"] -->|owns| FLT["FastLookupTable"]
        FTF -->|stores &table_ in| CTX["ProcessorContext.processor_data"]
    end

    subgraph "PmdThread"
        PT["PmdThread"] -->|member| CTX
        PT -->|passes ctx& to| LAUNCH["LauncherFn"]
        LAUNCH -->|constructs| FTF
    end

    subgraph "Control Plane"
        CH["CommandHandler"] -->|get_flow_table| PTM["PMDThreadManager"]
        PTM -->|GetThread(lcore)| PT
        PT -->|GetProcessorContext()| CTX
        CH -->|cast processor_data to| FLT
        CH -->|Phase 1: SetModifiable false| FLT
        CH -->|CallAfterGracePeriod| RCU["RcuManager"]
        RCU -->|grace period callback| CH
        CH -->|Phase 2: Begin/End iterate| ENTRIES["LookupEntry*"]
        CH -->|Phase 2: SetModifiable true| FLT
        CH -->|serialize + respond| JSON["JSON Response"]
    end

    subgraph "E2E Test"
        SCAPY["scapy sendp()"] -->|TAP| FTF
        TEST["pytest"] -->|ControlClient| CH
        TEST -->|verify entries| JSON
    end
```

### Key Design Decisions

**1. LauncherFn signature change: `const ProcessorContext&` → `ProcessorContext&`**

The launcher needs to write `processor_data` after constructing the processor. Changing to non-const reference is the minimal change. This is safe because the context is a `PmdThread` member with a well-defined lifetime.

**2. ProcessorContext as PmdThread member (not local in Run())**

Currently `ProcessorContext{&stats_}` is a temporary in `PmdThread::Run()`. Making it a member ensures the control plane can read `processor_data` after the thread starts. The `stats_` pointer is set in the constructor; `processor_data` is set by the launcher.

**3. Thread safety for processor_data**

The pointer is written once by the PMD thread during processor construction (inside the launcher) and read later by the control plane. The `rte_eal_remote_launch` / `rte_eal_wait_lcore` mechanism provides the happens-before relationship, so no atomic or mutex is needed for the pointer itself.

**4. Grace period via RcuManager::CallAfterGracePeriod (not sleep)**

`SetModifiable(false)` prevents new inserts but doesn't guarantee the PMD thread has finished its current batch. Instead of a blind `sleep_for()`, we use the existing `RcuManager::CallAfterGracePeriod()` which leverages DPDK's `rte_rcu_qsbr` mechanism. This starts an RCU grace period token and fires a callback once all registered PMD threads have passed through a quiescent state (i.e., completed their current `process_impl()` iteration and called `rte_rcu_qsbr_quiescent()`). This is both correct (guarantees the thread is done) and efficient (no arbitrary sleep duration).

Since `CallAfterGracePeriod` is asynchronous (the callback fires on the next poll timer tick after the grace period completes), `HandleGetFlowTable` becomes a two-phase operation:
- Phase 1: `SetModifiable(false)` on all tables, then schedule the read via `CallAfterGracePeriod`.
- Phase 2 (in callback): Iterate entries, serialize to JSON, `SetModifiable(true)`, send response via the deferred response callback.

This requires `CommandHandler` to have access to `RcuManager*` and to support deferred (async) responses for certain commands.

**5. Async response support in CommandHandler**

The `UnixSocketServer::MessageCallback` already provides a `response_callback` function that can be called asynchronously. Currently `ControlPlane::Run()` calls `HandleCommand()` synchronously and immediately invokes `response_callback`. For `get_flow_table`, the response is deferred: `HandleCommand` returns an empty string (or a sentinel), and the actual response is sent later via the captured `response_callback` when the grace period completes.

The cleanest approach: add a `HandleCommandAsync` method (or modify `HandleCommand` to accept an optional response callback). When the command is `get_flow_table`, the handler captures the response callback and defers the response. For all other commands, behavior is unchanged (synchronous).

**6. SetModifiable(true) in all paths (RAII-style)**

If an error occurs during table reading in the grace period callback, `SetModifiable(true)` must still be called. The implementation uses a scope guard or explicit try/catch to guarantee restoration.

**7. Removing flow_table_query_ callback**

The existing `std::function<int()> flow_table_query_` callback is replaced by direct access to `PMDThreadManager` + `ProcessorContext` + `RcuManager`. The `SetFlowTableQueryCallback` method and `flow_table_query_` member are removed.

**8. IP address formatting**

`IpAddress` is a union (`uint32_t v4` / `uint8_t v6[16]`). Serialization uses `inet_ntop(AF_INET, ...)` for IPv4 and `inet_ntop(AF_INET6, ...)` for IPv6, selected by `LookupEntry::IsIpv6()`.

## Components and Interfaces

### 1. ProcessorContext Extension

Modified file: `processor/processor_context.h`

```cpp
struct ProcessorContext {
  PacketStats* stats = nullptr;
  void* processor_data = nullptr;  // NEW: processor-specific data pointer
};
```

Backward compatible: existing processors that don't set `processor_data` leave it as nullptr.

### 2. LauncherFn Signature Change

Modified file: `processor/processor_registry.h`

```cpp
// BEFORE:
using LauncherFn = std::function<int(
    const dpdk_config::PmdThreadConfig& config,
    std::atomic<bool>* stop_flag,
    struct rte_rcu_qsbr* qsbr_var,
    const processor::ProcessorContext& ctx)>;

// AFTER:
using LauncherFn = std::function<int(
    const dpdk_config::PmdThreadConfig& config,
    std::atomic<bool>* stop_flag,
    struct rte_rcu_qsbr* qsbr_var,
    processor::ProcessorContext& ctx)>;  // non-const
```

### 3. MakeProcessorEntry — Store processor_data

Modified file: `processor/processor_registry.h`

The launcher lambda in `MakeProcessorEntry` is updated to:
1. Accept `ProcessorContext&` (non-const).
2. After constructing the processor, store the processor's data pointer (if any) into `ctx.processor_data`.

For `FiveTupleForwardingProcessor`, this means storing `&proc.table()` (a new public accessor returning `FastLookupTable<>*`). For processors without a table (like `SimpleForwardingProcessor`), `processor_data` remains nullptr.

To support this generically, each processor type provides an optional static or member function. The simplest approach: add a `GetProcessorData()` method to the processor base or use a template specialization. However, to keep changes minimal, the `MakeProcessorEntry` template can simply not set `processor_data` — instead, the `FiveTupleForwardingProcessor` launcher is specialized or the processor sets it in its constructor if given a context pointer.

**Chosen approach**: The `MakeProcessorEntry` launcher passes `ctx` by non-const reference. Each processor's constructor can optionally accept a `ProcessorContext*` and write `processor_data`. But to avoid changing all processor constructors, a simpler approach: after constructing the processor, call a free function or method that the processor can opt into. The cleanest minimal change:

```cpp
template <typename ProcessorType>
ProcessorEntry MakeProcessorEntry() {
  return ProcessorEntry{
      .launcher =
          [](const dpdk_config::PmdThreadConfig& config,
             std::atomic<bool>* stop_flag,
             struct rte_rcu_qsbr* qsbr_var,
             processor::ProcessorContext& ctx) -> int {
        ProcessorType proc(config, ctx.stats);
        proc.ExportProcessorData(ctx);  // no-op for base, sets pointer for FiveTuple
        while (!stop_flag->load(std::memory_order_relaxed)) {
          proc.process_impl();
          if (qsbr_var) {
            rte_rcu_qsbr_quiescent(qsbr_var, rte_lcore_id());
          }
        }
        return 0;
      },
      // ... checker and param_checker unchanged ...
  };
}
```

`PacketProcessorBase` provides a default no-op `ExportProcessorData(ProcessorContext&) {}`. `FiveTupleForwardingProcessor` overrides it to set `ctx.processor_data = &table_`.

### 4. FiveTupleForwardingProcessor — table() Accessor and ExportProcessorData

Modified file: `processor/five_tuple_forwarding_processor.h`

```cpp
class FiveTupleForwardingProcessor
    : public PacketProcessorBase<FiveTupleForwardingProcessor> {
 public:
  // ... existing interface ...

  // Expose table pointer for control plane access via ProcessorContext.
  void ExportProcessorData(ProcessorContext& ctx) {
    ctx.processor_data = &table_;
  }

 private:
  rxtx::FastLookupTable<> table_;
};
```

### 5. PmdThread — ProcessorContext as Member

Modified files: `config/pmd_thread.h`, `config/pmd_thread.cc`

```cpp
class PmdThread {
 public:
  // ... existing interface ...

  // Access the processor context (populated after Run() starts).
  const processor::ProcessorContext& GetProcessorContext() const { return ctx_; }

 private:
  // ... existing members ...
  processor::ProcessorContext ctx_;  // NEW: member instead of local in Run()
};
```

Constructor initializes `ctx_.stats = &stats_`. `Run()` passes `ctx_` by reference to the launcher:

```cpp
int PmdThread::Run() {
  // ... lookup processor ...
  return (*entry_or)->launcher(config_, stop_flag_ptr_, qsbr_var_, ctx_);
}
```

### 6. CommandHandler — Async HandleGetFlowTable with RCU Grace Period

Modified files: `control/command_handler.h`, `control/command_handler.cc`

Remove `flow_table_query_` member and `SetFlowTableQueryCallback`. Add `rcu::RcuManager* rcu_manager_` member (set via constructor or setter). Add a `ResponseCallback` type alias and modify `HandleCommand` to support deferred responses.

**New type alias:**
```cpp
using ResponseCallback = std::function<void(const std::string&)>;
```

**New method signature:**
```cpp
// Process a command. Returns the JSON response string for synchronous commands.
// For async commands (e.g., get_flow_table), the response is sent via response_cb
// and the method returns std::nullopt to signal "response deferred."
std::optional<std::string> HandleCommand(const std::string& json_command,
                                          ResponseCallback response_cb = nullptr);
```

**HandleGetFlowTable — Two-phase async implementation:**

Phase 1 (called synchronously from HandleCommand):
1. Collect all tables from threads with non-null `processor_data`.
2. Call `SetModifiable(false)` on each table.
3. Call `rcu_manager_->CallAfterGracePeriod(callback)` where the callback captures the tables, lcore IDs, and the `response_cb`.
4. Return `std::nullopt` (response will be sent asynchronously via `response_cb`).

Phase 2 (called by RcuManager after grace period completes):
1. Iterate each table's entries using `Begin()`/`End()`, serialize to JSON.
2. Call `SetModifiable(true)` on each table (with try/catch to guarantee restoration).
3. Format the full response and invoke `response_cb(response_string)`.

```cpp
void CommandHandler::HandleGetFlowTableAsync(ResponseCallback response_cb) {
  // Phase 1: Collect tables and pause modifications
  struct TableInfo {
    uint32_t lcore_id;
    rxtx::FastLookupTable<>* table;  // nullptr for non-FiveTuple threads
  };
  std::vector<TableInfo> tables;

  for (uint32_t lcore_id : thread_manager_->GetLcoreIds()) {
    PmdThread* thread = thread_manager_->GetThread(lcore_id);
    const auto& ctx = thread->GetProcessorContext();
    auto* table = static_cast<rxtx::FastLookupTable<>*>(ctx.processor_data);
    tables.push_back({lcore_id, table});
    if (table) {
      table->SetModifiable(false);
    }
  }

  // Phase 2: Schedule read after grace period
  rcu_manager_->CallAfterGracePeriod(
      [tables = std::move(tables), response_cb = std::move(response_cb), this]() {
    json threads_array = json::array();

    for (const auto& info : tables) {
      if (info.table == nullptr) {
        threads_array.push_back({{"lcore_id", info.lcore_id},
                                  {"entries", json::array()}});
        continue;
      }

      json entries = json::array();
      try {
        for (auto it = info.table->Begin(); it != info.table->End(); ++it) {
          rxtx::LookupEntry* entry = *it;
          json e;
          char ip_buf[INET6_ADDRSTRLEN];
          if (entry->IsIpv6()) {
            inet_ntop(AF_INET6, entry->src_ip.v6, ip_buf, sizeof(ip_buf));
            e["src_ip"] = ip_buf;
            inet_ntop(AF_INET6, entry->dst_ip.v6, ip_buf, sizeof(ip_buf));
            e["dst_ip"] = ip_buf;
          } else {
            inet_ntop(AF_INET, &entry->src_ip.v4, ip_buf, sizeof(ip_buf));
            e["src_ip"] = ip_buf;
            inet_ntop(AF_INET, &entry->dst_ip.v4, ip_buf, sizeof(ip_buf));
            e["dst_ip"] = ip_buf;
          }
          e["src_port"] = entry->src_port;
          e["dst_port"] = entry->dst_port;
          e["protocol"] = entry->protocol;
          e["vni"] = entry->vni;
          e["is_ipv6"] = entry->IsIpv6();
          entries.push_back(std::move(e));
        }
      } catch (...) {
        info.table->SetModifiable(true);
        // Send error response
        CommandResponse err;
        err.status = "error";
        err.error = "Failed to serialize flow table entries";
        response_cb(FormatResponse(err));
        return;
      }
      info.table->SetModifiable(true);
      threads_array.push_back({{"lcore_id", info.lcore_id},
                                {"entries", entries}});
    }

    CommandResponse resp;
    resp.status = "success";
    resp.result = {{"threads", threads_array}};
    response_cb(FormatResponse(resp));
  });
}
```

**ControlPlane wiring update** (`control/control_plane.cc`):

The `ControlPlane::Initialize` passes `rcu_manager_.get()` to `CommandHandler`. The socket server callback is updated to pass the response callback through:

```cpp
socket_server_->Start(
    [this](const std::string& message,
           std::function<void(const std::string&)> response_callback) {
      auto response = command_handler_->HandleCommand(message, response_callback);
      if (response.has_value()) {
        response_callback(*response);
      }
      // If nullopt, the command handler will call response_callback later (async)
    });
```

### 7. TestConfigGenerator — Processor Settings

Modified file: `tests/fixtures/config_generator.py`

Add `processor_name` and `processor_params` parameters to `generate_config()`:

```python
@staticmethod
def generate_config(
    num_ports=2, num_threads=2, num_queues=2,
    num_rx_queues=None, num_tx_queues=None,
    use_hugepages=False,
    processor_name=None,       # NEW
    processor_params=None      # NEW: dict of str->str
) -> Dict[str, Any]:
```

When `processor_name` is provided, each `pmd_threads` entry includes `"processor_name": "<name>"`. When `processor_params` is provided, each entry includes `"processor_params": {<dict>}`. When omitted, these fields are absent from the JSON (backward compatible).

### 8. generate_dpdk_config.py — CLI Arguments

Modified file: `generate_dpdk_config.py`

Add:
```python
parser.add_argument("--processor", type=str, help="Processor name")
parser.add_argument("--processor-param", action="append", metavar="KEY=VALUE",
                    help="Processor parameter (repeatable)")
```

Parse `--processor-param` values into a dict and pass to `generate_config()`.

### 9. conftest.py — test_config Fixture Extension

Modified file: `tests/conftest.py`

Extract `processor_name` and `processor_params` from the `request.param` dict and pass to `generate_config()`:

```python
processor_name = params.get('processor_name', None)
processor_params = params.get('processor_params', None)
```

### 10. ControlClient — get_flow_table Method

Modified file: `tests/fixtures/control_client.py`

```python
def get_flow_table(self) -> Dict[str, Any]:
    """Send get_flow_table command and return parsed response."""
    return self.send_command("get_flow_table")
```

## Data Models

### ProcessorContext (extended)

| Field | Type | Description |
|---|---|---|
| stats | PacketStats* | Per-thread packet statistics (existing) |
| processor_data | void* | Processor-specific data pointer, nullptr by default |

### get_flow_table Response (success)

```json
{
  "status": "success",
  "result": {
    "threads": [
      {
        "lcore_id": 1,
        "entries": [
          {
            "src_ip": "10.0.0.1",
            "dst_ip": "10.0.0.2",
            "src_port": 12345,
            "dst_port": 80,
            "protocol": 6,
            "vni": 0,
            "is_ipv6": false
          }
        ]
      }
    ]
  }
}
```

### get_flow_table Response (thread without flow table)

Threads whose `processor_data` is nullptr return an empty entries array:

```json
{
  "lcore_id": 2,
  "entries": []
}
```

### E2E Test Config Dict

```python
FIVE_TUPLE_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"}
}
```

### LookupEntry Serialization Mapping

| LookupEntry Field | JSON Key | JSON Type | Notes |
|---|---|---|---|
| src_ip | src_ip | string | inet_ntop formatted |
| dst_ip | dst_ip | string | inet_ntop formatted |
| src_port | src_port | integer | uint16_t |
| dst_port | dst_port | integer | uint16_t |
| protocol | protocol | integer | uint8_t (6=TCP, 17=UDP) |
| vni | vni | integer | uint32_t |
| IsIpv6() | is_ipv6 | boolean | derived from flags bit 0 |


## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: LookupEntry serialization contains all required fields

*For any* valid LookupEntry (with arbitrary src_ip, dst_ip, src_port, dst_port, protocol, vni, and flags values for both IPv4 and IPv6), serializing the entry to JSON should produce an object containing all seven required keys (`src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `vni`, `is_ipv6`) with correct types (strings for IPs, integers for ports/protocol/vni, boolean for is_ipv6), and the IP strings should be valid `inet_ntop` representations matching the original address bytes.

**Validates: Requirements 3.4, 3.7, 9.3**

### Property 2: get_flow_table response contains one element per thread

*For any* set of PmdThread instances managed by PMDThreadManager (with varying processor_data — some nullptr, some pointing to FastLookupTables with varying entry counts), the `get_flow_table` response `threads` array should contain exactly one element per thread, each with a valid `lcore_id` and an `entries` array whose length equals the number of entries in that thread's table (or 0 if processor_data is nullptr).

**Validates: Requirements 3.6, 3.8, 9.1, 9.2**

### Property 3: Config generator includes processor settings when provided

*For any* non-None processor_name string and any non-None processor_params dictionary (with string keys and string values), calling `TestConfigGenerator.generate_config()` with those arguments should produce a config where every element in the `pmd_threads` array contains a `processor_name` field equal to the input string and a `processor_params` field equal to the input dictionary. When processor_name is None, the `processor_name` key should be absent from all pmd_threads entries.

**Validates: Requirements 4.1, 4.2, 4.3**

## Error Handling

### Control Plane — HandleGetFlowTable

| Condition | Behavior |
|---|---|
| `processor_data` is nullptr for a thread | Include thread in response with empty `entries` array |
| Exception during table iteration | Call `SetModifiable(true)` before re-throwing; return error response |
| `thread_manager_` is nullptr | Return error response (defensive check) |
| Thread has no entries | Include thread with empty `entries` array |

### SetModifiable Protocol (Two-Phase with RCU Grace Period)

| Step | Phase | Action | Failure Mode |
|---|---|---|---|
| 1 | Phase 1 | `SetModifiable(false)` on all tables | Atomic store — cannot fail |
| 2 | Phase 1 | `CallAfterGracePeriod(callback)` | Returns error if RcuManager not running |
| 3 | Phase 2 | Iterate entries in grace period callback | May throw (JSON serialization OOM) |
| 4 | Phase 2 | `SetModifiable(true)` on each table | Must execute on all paths (try/catch) |

If step 3 fails, step 4 still executes via try/catch, ensuring the PMD thread can resume inserting flows. If step 2 fails (RcuManager not running), `SetModifiable(true)` is called immediately on all tables and an error response is returned synchronously.

### Python Test Infrastructure

| Condition | Behavior |
|---|---|
| `processor_name` not in config dict | Omit from generated JSON (backward compatible) |
| `--processor-param` with invalid format (no `=`) | CLI argument parser raises error |
| `get_flow_table` returns error | Test assertion fails with descriptive message |
| Flow table empty after sending packets | Test retries with sleep (packets may be in-flight) |

## Testing Strategy

### Unit Tests (C++)

- **ProcessorContext default**: Verify `processor_data` is nullptr on default construction.
- **ExportProcessorData**: Construct a `FiveTupleForwardingProcessor`, call `ExportProcessorData`, verify `processor_data` is non-null.
- **Backward compatibility**: Verify `SimpleForwardingProcessor` launcher leaves `processor_data` as nullptr.
- **HandleGetFlowTable with nullptr processor_data**: Mock a thread with nullptr processor_data, verify empty entries array in response.
- **HandleGetFlowTable serialization**: Insert known entries into a FastLookupTable, call HandleGetFlowTable, verify JSON fields match.
- **SetModifiable restoration on error**: Verify SetModifiable(true) is called even if iteration throws.
- **IP address formatting**: Verify IPv4 and IPv6 addresses are correctly formatted via inet_ntop.

### Unit Tests (Python)

- **Config generator with processor_name**: Verify `processor_name` appears in generated JSON.
- **Config generator without processor_name**: Verify `processor_name` is absent.
- **Config generator with processor_params**: Verify `processor_params` appears in generated JSON.
- **CLI --processor argument**: Run generate_dpdk_config.py with --processor, verify output JSON.
- **CLI --processor-param argument**: Run with --processor-param KEY=VALUE, verify output JSON.

### Property-Based Tests

Property-based tests use [RapidCheck](https://github.com/emil-e/rapidcheck) (C++) and [Hypothesis](https://hypothesis.readthedocs.io/) (Python) with a minimum of 100 iterations per property. Each test is tagged with a comment referencing its design property.

| Property | Library | Description |
|---|---|---|
| 1: LookupEntry serialization | RapidCheck | Generate random LookupEntry values (IPv4 and IPv6), serialize, verify all fields present with correct types and values |
| 2: Response thread count | RapidCheck | Generate random thread configurations with varying processor_data, verify response structure |
| 3: Config generator processor settings | Hypothesis | Generate random processor_name strings and processor_params dicts, verify generated config includes them |

Tag format: Each property test includes a comment:
```
// Feature: e2e-five-tuple-forwarding, Property N: <property title>
```

For Python:
```python
# Feature: e2e-five-tuple-forwarding, Property N: <property title>
```

### E2E Tests

E2E tests use pytest with the existing fixture infrastructure (`dpdk_process`, `control_client`, `tap_interfaces`, `test_config`). They are example-based, not property-based, since they involve real DPDK processes and TAP interfaces.

| Test | Validates |
|---|---|
| `test_single_ipv4_flow_entry` | Req 5.1 — Single IPv4 packet produces correct flow entry |
| `test_multiple_distinct_flows` | Req 6.1 — Multiple distinct packets produce distinct entries |
| `test_duplicate_packets_single_entry` | Req 7.1 — Duplicate packets don't create extra entries |
| `test_ipv6_flow_entry` | Req 8.1 — IPv6 packet produces correct entry with is_ipv6=true |
| `test_flow_table_response_structure` | Req 9.1, 9.2, 9.3 — Response JSON structure validation |

All e2e tests use the config:
```python
FIVE_TUPLE_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"}
}
```
