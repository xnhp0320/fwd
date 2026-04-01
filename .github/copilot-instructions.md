# Copilot instructions for `dpdk_bazel_xsight`

## Build, test, and lint commands

### Prerequisite: local DPDK path
- Copy and edit machine-local DPDK config before building:
  - `cp MODULE.bazel.local.example MODULE.bazel.local`
  - Set `new_local_repository(name = "dpdk", path = "...")` in `MODULE.bazel.local`.

### Bazel build
- Build main binary: `bazel build //:main`
- Build all targets: `bazel build //...`
- Release build: `bazel build --config=release //:main`
- Release + debug symbols: `bazel build --config=release-dbg //:main`

### Bazel tests
- Run all Bazel tests: `bazel test //...`
- Run a single C++ test target:
  - `bazel test //control:command_handler_test`
  - `bazel test //rxtx:fast_lookup_table_test`
  - `bazel test //session:session_table_test`
- Run a single Go test target:
  - `bazel test //fwdcli/client:client_test`
  - `bazel test //fwdcli/formatter:formatter_test`

### Python tests (pytest)
- Install deps: `pip install -r tests/requirements.txt`
- Run full Python suite: `pytest tests/`
- Run one file: `pytest tests/e2e/test_control_plane.py`
- Run one test: `pytest tests/e2e/test_control_plane.py::TestControlPlane::test_status_command`

### Lint/static analysis workflow used in this repo
- No dedicated repo lint target is defined in Bazel/README.
- Generate compile DB for `clangd`/editor diagnostics:
  - `./generate_compile_commands.py`
  - `./generate_compile_commands.py //:main //control:command_handler_test`

## High-level architecture

- `main.cc` orchestrates startup in three phases:
  1. Parse/validate JSON config (`config/`).
  2. Create PMD threads (not launched yet).
  3. Initialize control plane, then launch PMD threads, then run event loop.

- `config/` owns DPDK bootstrap and thread orchestration:
  - `DpdkInitializer` initializes DPDK/ports.
  - `PMDThreadManager` creates/launches worker threads and coordinates stop/wait.

- `processor/` is the dataplane plugin system:
  - `ProcessorRegistry` maps processor name -> launcher/checker/command registrar.
  - Concrete processors (simple/five_tuple/lpm/tbm) run packet hot loops.

- `control/` is the control plane runtime:
  - `ControlPlane` runs Boost.Asio event loop on main lcore only.
  - `UnixSocketServer` handles newline-delimited JSON over Unix socket.
  - `CommandHandler` dispatches built-in + processor-registered commands.

- Shared state wiring:
  - `ControlPlane::Initialize` creates optional shared resources (`session::SessionTable`, LPM/TBM FIB) and injects them into each thread’s `ProcessorContext`.
  - `rcu::RcuManager` is attached so PMD threads report QSBR quiescent states.

- `fwdcli/` is a separate Go CLI client (Cobra) for the Unix socket command API.

## Key repository-specific conventions

- Error handling convention is `absl::Status` / `absl::StatusOr<T>` throughout C++ modules; startup/runtime failures are surfaced to `main` and terminate with non-zero exit.

- Processor registration is static and linker-sensitive:
  - Register in `.cc` via `REGISTER_PROCESSOR("name", Type)`.
  - Corresponding `cc_library` must use `alwayslink = True` to keep static initializers.

- Processor execution pattern is CRTP-based:
  - Implement `check_impl(...)` for startup validation.
  - Implement `process_impl()` for hot path.
  - Runtime-specific pointers are passed through `ProcessorContext` (opaque `void*` fields by design for low-overhead generic launchers).

- Control/data-plane lcore split is strict:
  - Main lcore is reserved for control plane.
  - PMD launch path skips main lcore and validates processor queue assignments before launch.

- Session table concurrency model depends on DPDK hash + QSBR integration:
  - `SessionTable` uses `RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF | RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD`.
  - When QSBR is available, hash is attached via `rte_hash_rcu_qsbr_add(...)`.

- Command API is tag-oriented in `CommandHandler`:
  - Commands are registered with tags (`common`, `session`, `fib`, processor-specific).
  - `list_commands` supports filtering by tag.

- Python e2e tests assume Bazel-built binary location and privilege setup:
  - Fixture expects `bazel-bin/main`.
  - Fixture attempts `sudo setcap cap_net_admin+ep bazel-bin/main` for TAP interface tests.
