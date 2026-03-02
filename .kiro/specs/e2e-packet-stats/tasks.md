# Implementation Plan: E2E Packet Stats

## Overview

Add per-PMD-thread packet/byte statistics tracking, a `get_stats` control plane command, and end-to-end tests using Scapy and PTF. Implementation proceeds bottom-up: stats data structure → processor integration → control plane command → Python client → e2e tests.

## Tasks

- [x] 1. Create PacketStats class and ProcessorContext struct
  - [x] 1.1 Create `processor/packet_stats.h` header-only class
    - Define `PacketStats` with `std::atomic<uint64_t>` for `packets_` and `bytes_`
    - Implement `RecordBatch(uint16_t packet_count, uint64_t byte_count)` using relaxed load+add+store (single-writer optimization, no `fetch_add`)
    - Implement `GetPackets()` and `GetBytes()` with `memory_order_relaxed` loads
    - Add `packet_stats` cc_library target to `processor/BUILD`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

  - [x] 1.2 Create `processor/processor_context.h` struct
    - Define `ProcessorContext` with `PacketStats* stats = nullptr`
    - Add `processor_context` cc_library target to `processor/BUILD`
    - _Requirements: 2.1_

  - [ ]* 1.3 Write RapidCheck property test for PacketStats counter accuracy
    - **Property 1: RecordBatch counter accuracy**
    - Create `processor/packet_stats_test.cc` with RapidCheck test
    - Generate random sequences of `RecordBatch` calls, verify `GetPackets()` == sum of packet_count args and `GetBytes()` == sum of byte_count args
    - Add `packet_stats_test` cc_test target to `processor/BUILD`
    - **Validates: Requirements 1.2, 1.3**

  - [ ]* 1.4 Write RapidCheck property test for per-thread counter isolation
    - **Property 2: Per-thread counter isolation**
    - In `processor/packet_stats_test.cc`, add test that creates two `PacketStats` instances, calls `RecordBatch` on one, verifies the other remains unchanged
    - **Validates: Requirements 1.1**

- [x] 2. Integrate stats into processor and PMD thread
  - [x] 2.1 Update `LauncherFn` signature to accept `ProcessorContext`
    - In `processor/processor_registry.h`, change `LauncherFn` to `(config, stop_flag, qsbr_var, const ProcessorContext& ctx)`
    - Update `MakeProcessorEntry` template lambda to receive `ctx` and pass `ctx.stats` to the processor
    - _Requirements: 2.1_

  - [x] 2.2 Update `SimpleForwardingProcessor` to record stats
    - Add `PacketStats* stats_` member and constructor parameter to `SimpleForwardingProcessor`
    - In `process_impl()`, after each RX burst with `batch.Count() > 0`, compute total bytes via `rte_pktmbuf_pkt_len` and call `stats_->RecordBatch()`
    - Add `const PacketStats& GetStats() const` accessor
    - Update `processor/BUILD` deps to include `:packet_stats`
    - _Requirements: 2.1, 2.2, 2.3_

  - [x] 2.3 Update `PmdThread` to own `PacketStats` and populate `ProcessorContext`
    - Add `PacketStats stats_` member to `PmdThread`
    - Add `const PacketStats* GetStats() const` accessor to `PmdThread`
    - In `PmdThread::Run()`, create `ProcessorContext` with `stats = &stats_` and pass to launcher
    - Update `config/BUILD` deps to include `//processor:packet_stats` and `//processor:processor_context`
    - _Requirements: 1.1, 2.1_

  - [x] 2.4 Update all call sites for the new `LauncherFn` signature
    - Update `PmdThread::Run()` to pass `ProcessorContext` as 4th arg to `entry->launcher()`
    - Verify `MakeProcessorEntry` lambda signature matches
    - _Requirements: 2.1_

- [x] 3. Checkpoint - Ensure C++ code compiles
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Add `get_stats` control plane command
  - [x] 4.1 Add `HandleGetStats()` to `CommandHandler`
    - Add `HandleGetStats(const nlohmann::json& params)` method declaration to `control/command_handler.h`
    - Implement in `control/command_handler.cc`: iterate `thread_manager_->GetLcoreIds()`, read each thread's `PacketStats`, build JSON with `threads` array and `total` object
    - Add dispatch in `ExecuteCommand()`: `else if (request.command == "get_stats") { return HandleGetStats(request.params); }`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6_

  - [ ]* 4.2 Write RapidCheck property test for stats response structure
    - **Property 3: Stats response contains required fields**
    - Extend `control/command_handler_test.cc` (or create if needed) to verify `get_stats` response JSON has `threads` array with `lcore_id`, `packets`, `bytes` per entry and `total` with `packets`, `bytes`
    - **Validates: Requirements 3.1, 3.2, 3.5**

  - [ ]* 4.3 Write RapidCheck property test for total equals sum of per-thread
    - **Property 4: Total stats equal sum of per-thread stats**
    - Test that `total.packets` == sum of `threads[i].packets` and `total.bytes` == sum of `threads[i].bytes` for randomly generated thread stats
    - **Validates: Requirements 3.3, 7.5**

- [x] 5. Checkpoint - Ensure C++ builds and unit tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Set up Python venv, test dependencies, and control client method
  - [x] 6.1 Add Scapy, PTF, and Hypothesis to `tests/requirements.txt`
    - Append `scapy>=2.5.0`, `ptf>=0.9.3`, `hypothesis>=6.0.0`
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 6.2 Create `tests/scripts/setup_venv.sh` helper script
    - Create venv at `tests/.venv` from system Python
    - Install deps from `tests/requirements.txt` via `tests/.venv/bin/pip`
    - Make script executable
    - Add `tests/.venv/` to `.gitignore`
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 6.3 Add `get_stats()` method to `tests/fixtures/control_client.py`
    - Add `get_stats()` method that calls `self.send_command("get_stats")` and returns parsed response
    - _Requirements: 4.1, 4.2_

- [x] 7. Create E2E packet stats tests
  - [x] 7.1 Create `tests/e2e/test_packet_stats.py` with test class and helpers
    - Create `TestPacketStats` class following existing test conventions
    - Add helper to bring TAP interface up (`ip link set <iface> up`)
    - Add helper to send packets via Scapy `sendp()` and receive via `sniff()`
    - Reuse `dpdk_process`, `control_client`, `tap_interfaces`, `test_config` fixtures
    - _Requirements: 8.1, 8.2, 8.3, 8.4_

  - [x] 7.2 Implement `test_stats_baseline_zero`
    - Query `get_stats` before sending traffic, verify counters are zero
    - _Requirements: 7.3_

  - [x] 7.3 Implement `test_packet_forwarding`
    - Send crafted Ethernet/IP packets via Scapy `sendp()` into TAP device
    - Use Scapy `sniff()` with timeout to capture forwarded packets
    - Verify received packet matches sent packet in protocol fields and payload using PTF `verify_packet` or manual assertion
    - Fail with descriptive message if no packet received within timeout
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [x] 7.4 Implement `test_stats_after_traffic`
    - Send N packets of known size (e.g., 84 bytes each: 14 Ether + 20 IP + 50 payload)
    - Query `get_stats`, verify `total.packets == N` and `total.bytes == N * 84`
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [x] 7.5 Implement `test_multi_thread_stats_sum`
    - Configure 2 PMD threads via test parametrize
    - Send traffic, query `get_stats`, verify sum of per-thread counters equals total
    - _Requirements: 7.5, 3.3_

  - [ ]* 7.6 Write Hypothesis property test for stats accuracy
    - **Property 6: Stats accuracy after known traffic**
    - Use Hypothesis to generate random packet counts and sizes, send through TAP, verify stats match
    - **Validates: Requirements 7.1, 7.2**

- [x] 8. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- C++ code uses Bazel build system; update BUILD files alongside source changes
- RapidCheck is already available via `MODULE.bazel`
- The `ProcessorContext` struct is designed for future extensibility without changing `LauncherFn` signature
- Packet size of 84 bytes (14 + 20 + 50) is chosen for deterministic byte count verification in e2e tests
- Python tests run in a venv at `tests/.venv`; run `tests/scripts/setup_venv.sh` to bootstrap, then `sudo tests/.venv/bin/pytest tests/e2e/` to execute
