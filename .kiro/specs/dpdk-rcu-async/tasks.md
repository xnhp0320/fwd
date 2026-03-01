# Implementation Plan: DPDK RCU Async Grace Period System

## Overview

Implement an asynchronous RCU system bridging DPDK's QSBR library with the boost.asio control plane. Build bottom-up: DeferredWorkItem struct, then the Vyukov MPSC queue (testable in isolation without DPDK), then RcuManager, then integrate into the existing PMD thread pipeline by replacing the LauncherFn signature and wiring RCU through PMDThreadManager and PmdThread.

## Tasks

- [x] 1. Create rcu directory with DeferredWorkItem and MpscQueue
  - [x] 1.1 Create `rcu/deferred_work_item.h` with the intrusive node struct
    - Define `DeferredAction` type alias (`std::function<void()>`)
    - Define `DeferredWorkItem` struct with `std::atomic<DeferredWorkItem*> next`, `uint64_t token`, and `DeferredAction callback`
    - _Requirements: 9.4, 9.6_

  - [x] 1.2 Create `rcu/mpsc_queue.h` implementing the Vyukov intrusive MPSC queue
    - Implement `MpscQueue` class with `stub_`, `head_` (atomic), `tail_` (plain pointer)
    - Implement `Push()` (wait-free: exchange + store) per Vyukov algorithm
    - Implement `Pop()` (lock-free single consumer) per Vyukov algorithm with stub re-insertion
    - Implement `Empty()` approximate check
    - Constructor initializes stub sentinel: `head_ = &stub_`, `tail_ = &stub_`
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.7_

  - [x] 1.3 Create `rcu/BUILD` with `deferred_work_item` and `mpsc_queue` library targets and test targets
    - `deferred_work_item`: header-only cc_library
    - `mpsc_queue`: header-only cc_library depending on `deferred_work_item`
    - `mpsc_queue_test`: cc_test depending on `mpsc_queue`, googletest, rapidcheck
    - `rcu_manager` and `rcu_manager_test` targets (can be stubs initially, filled in task 3)
    - _Requirements: 9.1_

  - [x] 1.4 Write property test: MPSC queue preserves all items under concurrent push
    - **Property 1: MPSC Queue Preserves All Items Under Concurrent Push**
    - Spawn M threads (M ∈ [1, 8]), each pushing K items (K ∈ [1, 1000]). Single consumer pops all. Verify total count = M × K, no items lost or duplicated.
    - **Validates: Requirements 9.7, 10.6**

  - [x] 1.5 Write unit tests for MpscQueue
    - Test empty pop returns nullptr
    - Test single push/pop returns correct item, next pop returns nullptr
    - Test push-push-pop-pop sequence preserves FIFO order per producer
    - Test stub re-insertion edge case (single remaining node)
    - _Requirements: 9.2, 9.5_

- [x] 2. Checkpoint - Ensure MPSC queue tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 3. Implement RcuManager core
  - [x] 3.1 Create `rcu/rcu_manager.h` with the RcuManager class declaration
    - Define `Config` struct with `max_threads` and `poll_interval_ms`
    - Declare `Init()`, `RegisterThread()`, `UnregisterThread()`, `GetQsbrVar()`, `CallAfterGracePeriod()`, `PostDeferredWork()`, `Start()`, `Stop()`, `IsRunning()`
    - Private members: `qsbr_var_`, `config_`, `io_ctx_`, `timer_`, `mpsc_queue_`, `pending_`, `running_`, `registration_mu_`, `registered_threads_`
    - Document thread safety for each public method
    - _Requirements: 1.1, 1.4, 4.2, 4.5, 7.1, 7.2, 7.4_

  - [x] 3.2 Create `rcu/rcu_manager.cc` with the RcuManager implementation
    - `Init()`: allocate QSBR variable via `rte_zmalloc`, call `rte_rcu_qsbr_init`, create steady_timer
    - `RegisterThread()`: mutex-guarded duplicate check, call `rte_rcu_qsbr_thread_register` + `rte_rcu_qsbr_thread_online`
    - `UnregisterThread()`: mutex-guarded presence check, call `rte_rcu_qsbr_thread_offline` + `rte_rcu_qsbr_thread_unregister`
    - `CallAfterGracePeriod()`: check running state, call `rte_rcu_qsbr_start`, create DeferredWorkItem, push to pending list, arm timer
    - `PostDeferredWork()`: delegate to `mpsc_queue_.Push()`
    - `Start()`: set `running_ = true`, arm timer if pending work exists
    - `Stop()`: set `running_ = false`, cancel timer, drain MPSC queue, clear pending list
    - `OnPollTimer()`: drain MPSC queue, process pending items (check tokens, invoke completed callbacks), re-arm if work remains
    - Destructor: call `Stop()`, `rte_free(qsbr_var_)`
    - _Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3, 2.4, 4.1, 4.2, 4.3, 4.4, 4.5, 5.1, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.3, 6.4, 10.2, 10.3, 10.4, 10.5_

  - [x] 3.3 Update `rcu/BUILD` with the `rcu_manager` library target
    - Depend on `deferred_work_item`, `mpsc_queue`, `//:dpdk_lib`, `@abseil-cpp//absl/status`, `@boost.asio`
    - _Requirements: 1.1_

  - [ ]* 3.4 Write property test: thread registration round trip
    - **Property 2: Thread Registration Round Trip**
    - Generate random valid lcore IDs (< max_threads), register then unregister each, verify success and re-registrability.
    - **Validates: Requirements 2.1, 2.2**

  - [ ]* 3.5 Write property test: registration rejects out-of-range thread IDs
    - **Property 3: Registration Rejects Out-of-Range Thread IDs**
    - Generate random lcore IDs >= max_threads, verify `RegisterThread` returns error.
    - **Validates: Requirements 2.3**

  - [ ]* 3.6 Write property test: registration rejects duplicate thread IDs
    - **Property 4: Registration Rejects Duplicate Thread IDs**
    - Generate random valid lcore ID, register it, register again without unregistering, verify second call returns `AlreadyExistsError`.
    - **Validates: Requirements 2.4**

  - [ ]* 3.7 Write property test: grace period callbacks fire after quiescent states
    - **Property 5: Grace Period Callbacks Fire After Quiescent States**
    - Enqueue N callbacks via `CallAfterGracePeriod`, simulate quiescent states on all registered threads, run `io_context`, verify all N callbacks were invoked.
    - **Validates: Requirements 4.1, 4.3, 10.3**

  - [ ]* 3.8 Write property test: callbacks fire in FIFO order
    - **Property 6: Callbacks Fire in FIFO Order**
    - Enqueue N callbacks with sequential markers, verify invocation order matches enqueue order (earliest token first).
    - **Validates: Requirements 4.4**

  - [ ]* 3.9 Write property test: stop discards all pending callbacks
    - **Property 7: Stop Discards All Pending Callbacks**
    - Enqueue N callbacks, call `Stop()`, run `io_context`, verify zero callbacks were invoked and pending list is empty.
    - **Validates: Requirements 6.1**

  - [ ]* 3.10 Write property test: CallAfterGracePeriod returns error after stop
    - **Property 8: CallAfterGracePeriod Returns Error After Stop**
    - Stop the manager, attempt `CallAfterGracePeriod` with a callable, verify error status returned and callable not invoked.
    - **Validates: Requirements 6.4**

  - [ ]* 3.11 Write property test: MPSC drain processes all available items per tick
    - **Property 9: MPSC Drain Processes All Available Items Per Tick**
    - Push N items into MPSC queue, trigger one poll timer tick, verify all N items appear in the pending list.
    - **Validates: Requirements 10.2, 10.6, 5.2**

  - [ ]* 3.12 Write property test: items with incomplete grace periods remain pending
    - **Property 10: Items With Incomplete Grace Periods Remain Pending**
    - Enqueue items, do NOT report quiescent states, trigger poll tick, verify no callbacks fired and items remain in pending list.
    - **Validates: Requirements 10.4**

  - [ ]* 3.13 Write unit tests for RcuManager lifecycle
    - Test Init → Start → Stop → destroy sequence
    - Test Init → destroy (without Start) is safe
    - Test double Stop is safe
    - Test timer idle optimization: timer stops when no work pending, restarts when work arrives
    - _Requirements: 1.1, 1.2, 1.3, 6.1, 6.2, 6.3, 5.3, 5.4_

- [x] 4. Checkpoint - Ensure RcuManager tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Replace LauncherFn signature and update ProcessorRegistry
  - [x] 5.1 Update `LauncherFn` in `processor/processor_registry.h` to accept `rte_rcu_qsbr*`
    - Change `LauncherFn` signature from `(const PmdThreadConfig&, std::atomic<bool>*)` to `(const PmdThreadConfig&, std::atomic<bool>*, struct rte_rcu_qsbr*)`
    - Remove the old non-RCU launcher — there is only one launcher now
    - Add forward declaration of `struct rte_rcu_qsbr` and include `<rte_rcu_qsbr.h>` in the header
    - _Requirements: 3.1, 3.2, 8.4_

  - [x] 5.2 Update `MakeProcessorEntry<T>()` template to generate the RCU-aware launcher
    - The lambda accepts `rte_rcu_qsbr* qsbr_var` as third parameter
    - After `proc.process_impl()`, call `rte_rcu_qsbr_quiescent(qsbr_var, rte_lcore_id())` only when `qsbr_var != nullptr`
    - _Requirements: 3.1, 3.2, 8.4_

  - [x] 5.3 Update `PmdThread` to pass QSBR variable to the launcher
    - Add `rte_rcu_qsbr* qsbr_var_` member to `PmdThread` (set via constructor or setter)
    - Update `PmdThread::Run()` to pass `qsbr_var_` as third argument to `entry->launcher()`
    - Update `PmdThread` constructor to accept optional `rte_rcu_qsbr*` parameter (default `nullptr`)
    - _Requirements: 3.1, 3.2, 8.4_

  - [x] 5.4 Update `processor/BUILD` to add `//:dpdk_lib` dep if not already present (for `rte_rcu_qsbr.h`)
    - _Requirements: 3.1_

- [x] 6. Integrate RcuManager into PMDThreadManager
  - [x] 6.1 Add `SetRcuManager()` and `rcu_manager_` member to `PMDThreadManager`
    - Add `rcu::RcuManager* rcu_manager_ = nullptr` private member
    - Add `void SetRcuManager(rcu::RcuManager* rcu_manager)` public method
    - Add `#include "rcu/rcu_manager.h"` to the header
    - _Requirements: 8.3_

  - [x] 6.2 Update `PMDThreadManager::LaunchThreads()` to register threads and pass QSBR var
    - Resolve `qsbr_var` once: `rcu_manager_ ? rcu_manager_->GetQsbrVar() : nullptr`
    - Before `rte_eal_remote_launch`, call `rcu_manager_->RegisterThread(config.lcore_id)` if RCU manager is set; propagate error on failure
    - Pass `qsbr_var` to `PmdThread` constructor so `Run()` forwards it to the launcher
    - _Requirements: 8.1, 8.3, 8.4_

  - [x] 6.3 Update `PMDThreadManager::WaitForThreads()` to unregister threads after wait
    - After all `rte_eal_wait_lcore` calls complete, unregister each lcore ID from the RCU manager if set
    - _Requirements: 8.2_

  - [x] 6.4 Update `config/BUILD` to add `//rcu:rcu_manager` dep to `pmd_thread_manager` target
    - _Requirements: 8.1_

  - [ ]* 6.5 Write unit test for backward compatibility: PMDThreadManager with nullptr RcuManager
    - Verify that `LaunchThreads` works without an RCU manager set (passes `nullptr` as qsbr_var to launcher, which skips quiescent reporting)
    - _Requirements: 8.3, 8.4_

- [x] 7. Wire RcuManager into main binary
  - [x] 7.1 Update root `BUILD` to add `//rcu:rcu_manager` dep to the `main` binary target
    - _Requirements: 1.1_

  - [x] 7.2 Update `main.cc` (or the ControlPlane initialization path) to create, init, and start the RcuManager
    - Create `RcuManager` instance, call `Init()` with the `io_context` and config
    - Call `pmd_thread_manager.SetRcuManager(rcu_manager.get())` before `LaunchThreads`
    - Call `rcu_manager->Start()` before entering the event loop
    - Call `rcu_manager->Stop()` during shutdown (before stopping PMD threads or after, per shutdown ordering)
    - _Requirements: 1.1, 8.1, 8.3_

- [x] 8. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- MPSC queue tests (task 1) have no DPDK dependency and can run in any environment
- RcuManager tests (task 3) require DPDK EAL initialization or mock QSBR interface
- Property tests use RapidCheck (already in MODULE.bazel)
- The LauncherFn signature change (task 5) is a breaking change — all processor registrations are updated via the `MakeProcessorEntry<T>()` template, so no manual fixup of individual processors is needed
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
