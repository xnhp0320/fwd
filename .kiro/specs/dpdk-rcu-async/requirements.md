# Requirements Document

## Introduction

This feature adds an asynchronous Read-Copy-Update (RCU) system to the DPDK-based packet processing application. It integrates DPDK's built-in RCU QSBR (Quiescent State Based Reclamation) library (`rte_rcu_qsbr`) with the existing boost.asio control plane event loop to provide non-blocking, callback-driven grace period completion.

The core idea: PMD threads (readers) report quiescent states as part of their poll loop. The control plane (writer) can update shared data structures and defer cleanup via a callback that fires once all PMD threads have passed through a quiescent state — confirming no reader holds a reference to the old data. A periodic boost.asio timer checks whether grace periods have elapsed, and invokes the registered callbacks when safe.

Additionally, PMD threads can submit deferred work requests to the control plane via a lock-free MPSC (Multiple Producer, Single Consumer) intrusive queue based on the Vyukov algorithm. This allows PMD threads to obtain an RCU token and package it with a deferred action callback, pushing it into the MPSC queue without locks or fixed-size limitations. The control plane drains this queue as part of its periodic poll timer tick, checking each item's grace period status and either invoking the callback immediately or moving it to the pending list for later processing. This provides a second path for enqueuing deferred work alongside the control plane's own `call_after_grace_period()` API.

### DPDK RCU QSBR Mechanism

DPDK's `rte_rcu_qsbr` implements Quiescent State Based Reclamation. The mechanism works as follows:

- A QSBR variable (`struct rte_rcu_qsbr`) is allocated and initialized with the maximum number of threads.
- Each reader thread registers itself via `rte_rcu_qsbr_thread_register()` and marks itself online via `rte_rcu_qsbr_thread_online()`.
- In the hot loop, each reader thread calls `rte_rcu_qsbr_quiescent()` to report that it has passed through a quiescent state (i.e., it does not hold references to any RCU-protected data).
- A writer initiates a grace period by calling `rte_rcu_qsbr_start()`, which returns a token (uint64_t). This token captures the current state of all registered threads.
- The writer later calls `rte_rcu_qsbr_check()` with that token to test whether all registered threads have reported a quiescent state since the token was issued. This call is non-blocking — it returns true/false immediately.
- When `rte_rcu_qsbr_check()` returns true, the grace period is complete and it is safe to free the old data.

This is a polling-based model: the writer must repeatedly check the token until all readers have caught up. The async integration wraps this polling in a boost.asio timer so the control plane never blocks.

## Glossary

- **RCU_Manager**: The central component that owns the QSBR variable, manages thread registration, and coordinates grace periods with the asio event loop.
- **QSBR_Variable**: An instance of `struct rte_rcu_qsbr` from DPDK's RCU library, the shared state that tracks quiescent state counters for all registered threads.
- **Grace_Period**: The interval between a writer requesting reclamation and all registered reader threads having reported at least one quiescent state, making it safe to free old data.
- **Token**: A `uint64_t` value returned by `rte_rcu_qsbr_start()` that captures the quiescent state snapshot at the time of the call. Used to check whether all threads have since reported a quiescent state.
- **Quiescent_State**: A point in a reader thread's execution where it holds no references to RCU-protected data. Reported by calling `rte_rcu_qsbr_quiescent()`.
- **PMD_Thread**: A DPDK poll-mode driver thread running on a dedicated lcore, executing a tight packet processing loop. These are the RCU reader threads.
- **Control_Plane**: The boost.asio-based event loop running on the main lcore, responsible for management operations. This is where RCU writer operations and grace period polling occur.
- **Deferred_Action**: A user-supplied callback (`std::function` or move-only callable) that the RCU_Manager invokes once the associated Grace_Period completes.
- **Poll_Timer**: A periodic boost.asio steady_timer that fires on the control plane event loop to check whether outstanding Grace_Periods have completed.
- **MPSC_Queue**: A lock-free Multiple Producer Single Consumer intrusive queue based on the Vyukov algorithm (from 1024cores.blogspot.com), used to pass deferred work requests from PMD_Threads to the Control_Plane without locks or fixed-size limitations. Producers (PMD_Threads) enqueue with a single XCHG instruction (wait-free), and the consumer (Control_Plane) dequeues in a lock-free manner. The queue is intrusive (nodes embed the link pointer), unbounded, and ABA-free.

## Requirements

### Requirement 1: QSBR Variable Lifecycle

**User Story:** As a control plane developer, I want the RCU system to manage the QSBR variable lifecycle, so that I have a correctly initialized RCU context for thread registration and grace period tracking.

#### Acceptance Criteria

1. WHEN the RCU_Manager is constructed with a `boost::asio::io_context` reference and a maximum thread count, THE RCU_Manager SHALL allocate and initialize a QSBR_Variable using `rte_rcu_qsbr_init()` sized for the specified maximum thread count.
2. IF `rte_rcu_qsbr_init()` fails, THEN THE RCU_Manager SHALL return an error status indicating the failure reason.
3. WHEN the RCU_Manager is destroyed, THE RCU_Manager SHALL free the QSBR_Variable memory.
4. THE RCU_Manager SHALL expose a method to retrieve a pointer to the QSBR_Variable so that PMD_Thread code can call quiescent state reporting functions directly.

### Requirement 2: Thread Registration

**User Story:** As a PMD thread launcher, I want to register and unregister threads with the RCU system, so that the grace period mechanism tracks the correct set of active reader threads.

#### Acceptance Criteria

1. WHEN a PMD_Thread is registered with a given lcore ID, THE RCU_Manager SHALL call `rte_rcu_qsbr_thread_register()` and `rte_rcu_qsbr_thread_online()` for that thread ID.
2. WHEN a PMD_Thread is unregistered, THE RCU_Manager SHALL call `rte_rcu_qsbr_thread_offline()` and `rte_rcu_qsbr_thread_unregister()` for that thread ID.
3. IF registration is attempted for a thread ID that exceeds the configured maximum thread count, THEN THE RCU_Manager SHALL return an error status.
4. IF registration is attempted for a thread ID that is already registered, THEN THE RCU_Manager SHALL return an error status indicating duplicate registration.

### Requirement 3: Quiescent State Reporting in PMD Threads

**User Story:** As a packet processor developer, I want PMD threads to report quiescent states during their poll loop, so that the RCU system can determine when grace periods have elapsed.

#### Acceptance Criteria

1. THE PMD_Thread SHALL call `rte_rcu_qsbr_quiescent()` on the QSBR_Variable once per iteration of the packet processing poll loop, after completing a batch of packet processing.
2. THE quiescent state reporting call SHALL use the lcore ID of the calling PMD_Thread as the thread identifier.
3. THE quiescent state reporting call SHALL have negligible impact on the packet processing hot path (the call itself is a single atomic store in DPDK's implementation).

### Requirement 4: Async Grace Period with Callback

**User Story:** As a control plane developer, I want to schedule a callback that executes after a grace period completes, so that I can safely free or reclaim old data without blocking the event loop.

#### Acceptance Criteria

1. WHEN `call_after_grace_period(Fn)` is called with a callable `Fn`, THE RCU_Manager SHALL call `rte_rcu_qsbr_start()` to obtain a Token and store `Fn` together with the Token in a pending queue.
2. THE RCU_Manager SHALL accept any callable type for `Fn`, including `std::function`, lambdas, and move-only callables (e.g., lambdas capturing `std::unique_ptr`).
3. WHEN the Poll_Timer fires and `rte_rcu_qsbr_check()` returns true for a pending Token, THE RCU_Manager SHALL invoke the associated Deferred_Action on the control plane event loop thread.
4. THE RCU_Manager SHALL process completed grace periods in FIFO order — Deferred_Actions with earlier Tokens SHALL be invoked before those with later Tokens.
5. THE `call_after_grace_period` method SHALL be callable only from the control plane event loop thread (the thread running `io_context::run()`).
6. THE pending queue SHALL contain items from both `call_after_grace_period` invocations and items drained from the MPSC_Queue (see Requirement 10).

### Requirement 5: Poll Timer Integration

**User Story:** As a system architect, I want the grace period polling to be driven by a boost.asio timer, so that it integrates naturally with the existing event loop without busy-waiting or spawning additional threads.

#### Acceptance Criteria

1. WHEN the RCU_Manager is started, THE RCU_Manager SHALL create a periodic boost.asio `steady_timer` that fires at a configurable interval (default: 1 millisecond).
2. WHEN the Poll_Timer fires, THE RCU_Manager SHALL first drain all available items from the MPSC_Queue into the pending list, then iterate over all pending Tokens and call `rte_rcu_qsbr_check()` for each.
3. WHILE no Deferred_Actions are pending and the MPSC_Queue is empty, THE RCU_Manager SHALL stop the Poll_Timer to avoid unnecessary wakeups on the event loop.
4. WHEN a new Deferred_Action is enqueued (via `call_after_grace_period` or via the MPSC_Queue) and the Poll_Timer is not running, THE RCU_Manager SHALL restart the Poll_Timer.
5. WHEN the RCU_Manager is stopped, THE RCU_Manager SHALL cancel the Poll_Timer.

### Requirement 6: Graceful Shutdown

**User Story:** As a system operator, I want the RCU system to handle shutdown cleanly, so that pending callbacks are either completed or discarded without resource leaks.

#### Acceptance Criteria

1. WHEN the RCU_Manager is stopped, THE RCU_Manager SHALL cancel the Poll_Timer and discard all pending Deferred_Actions without invoking them.
2. WHEN the RCU_Manager is stopped, THE RCU_Manager SHALL allow in-progress Deferred_Action invocations to complete before returning.
3. WHEN the RCU_Manager is destroyed, THE RCU_Manager SHALL call stop if it has not already been stopped.
4. IF `call_after_grace_period` is called after the RCU_Manager has been stopped, THEN THE RCU_Manager SHALL return an error status indicating the manager is not running.

### Requirement 7: Thread Safety Boundaries

**User Story:** As a developer, I want clear thread safety guarantees, so that I know which operations are safe to call from which threads.

#### Acceptance Criteria

1. THE RCU_Manager SHALL be safe to call `call_after_grace_period`, `start`, and `stop` from the control plane event loop thread only.
2. THE `register_thread` and `unregister_thread` methods SHALL be safe to call from any thread, as they are typically called during PMD_Thread setup and teardown which may occur on different threads.
3. THE QSBR_Variable pointer returned by the RCU_Manager SHALL be safe for PMD_Threads to use concurrently for `rte_rcu_qsbr_quiescent()` calls, as guaranteed by DPDK's RCU library.
4. THE RCU_Manager SHALL document thread safety requirements for each public method.

### Requirement 8: Integration with PMDThreadManager

**User Story:** As a system integrator, I want the RCU system to integrate with the existing PMDThreadManager, so that thread registration and quiescent state reporting happen automatically as part of the existing thread lifecycle.

#### Acceptance Criteria

1. WHEN `PMDThreadManager::LaunchThreads` launches a PMD_Thread, THE PMDThreadManager SHALL register the thread's lcore ID with the RCU_Manager before the thread enters its processing loop.
2. WHEN a PMD_Thread is stopped and has exited its processing loop, THE PMDThreadManager SHALL unregister the thread's lcore ID from the RCU_Manager.
3. THE PMDThreadManager SHALL accept an optional pointer to the RCU_Manager — WHEN no RCU_Manager is provided, THE PMDThreadManager SHALL operate without RCU support (backward compatible).
4. THE PmdThread poll loop SHALL call `rte_rcu_qsbr_quiescent()` only WHEN an RCU_Manager has been configured.


### Requirement 9: MPSC Deferred Work Queue

**User Story:** As a PMD thread developer, I want a lock-free queue to submit deferred work requests to the control plane, so that PMD threads can request RCU-deferred actions without taking locks or being limited by a fixed-size ring buffer.

#### Acceptance Criteria

1. THE RCU_Manager SHALL provide a global MPSC_Queue for passing deferred work requests from PMD_Threads (producers) to the Control_Plane (single consumer).
2. THE MPSC_Queue SHALL be lock-free for MPSC operation — wait-free for producers (single XCHG instruction) and lock-free for the consumer.
3. THE MPSC_Queue SHALL NOT use `rte_ring`, as the queue must support unbounded (non-fixed) length.
4. THE MPSC_Queue SHALL use an intrusive data structure design where each node embeds its own link pointer, requiring no separate heap allocation per enqueue operation.
5. THE MPSC_Queue SHALL implement the Vyukov intrusive MPSC queue algorithm, which is unbounded and ABA-free.
6. THE RCU_Manager SHALL define a deferred work item node structure containing: a `next` pointer for the intrusive list, an RCU Token (obtained via `rte_rcu_qsbr_start()` or passed from the Control_Plane), and a Deferred_Action callback.
7. THE MPSC_Queue SHALL be safe for multiple PMD_Threads to enqueue concurrently without mutual exclusion.

### Requirement 10: PMD-Initiated Deferred Work Flow

**User Story:** As a PMD thread developer, I want to post deferred work requests from the PMD thread into the MPSC queue, so that the control plane can execute the deferred action once the grace period completes without the PMD thread blocking or coordinating directly with the control plane.

#### Acceptance Criteria

1. WHEN a PMD_Thread needs to defer work, THE PMD_Thread SHALL obtain a Token by calling `rte_rcu_qsbr_start()`, package the Token and the Deferred_Action into a deferred work item node, and push the node into the MPSC_Queue.
2. WHEN the Poll_Timer fires, THE Control_Plane SHALL drain all available items from the MPSC_Queue in one batch.
3. WHEN a dequeued item's Token satisfies `rte_rcu_qsbr_check()` (grace period complete), THE Control_Plane SHALL invoke the associated Deferred_Action immediately.
4. WHEN a dequeued item's Token does not satisfy `rte_rcu_qsbr_check()` (grace period not yet complete), THE Control_Plane SHALL move the item into the pending list used by `call_after_grace_period` (Requirement 4) to be checked again on subsequent Poll_Timer ticks.
5. THE RCU_Manager SHALL support two paths for enqueuing deferred work: (1) `call_after_grace_period()` called from the Control_Plane thread (Requirement 4), and (2) PMD_Threads posting through the MPSC_Queue.
6. THE MPSC_Queue drain SHALL process all available items per Poll_Timer tick to minimize latency and amortize the cost of queue access.
