# Implementation Plan: Flow Table Garbage Collection

## Overview

Implement LRU-based flow table garbage collection in three layers: (1) PmdJobRunner auto-return refactor, (2) LRU tracking infrastructure in FastLookupTable via a parallel LruNode array, (3) traffic-aware GC scheduling in FiveTupleForwardingProcessor. Each task builds incrementally so that foundational changes land first and integration wires everything together last.

## Tasks

- [x] 1. PmdJobRunner auto-return refactor
  - [x] 1.1 Modify `RunRunnableJobs()` in `processor/pmd_job.h` to drain `runner_jobs_` back to `pending_jobs_` after executing all callbacks, setting each job's state to `kPending`
    - After the existing iteration loop, add a `while (!runner_jobs_.empty())` loop that pops each job from `runner_jobs_`, sets `state_ = kPending`, and pushes to `pending_jobs_`
    - _Requirements: 1.1, 1.2, 1.3, 1.5_

  - [x] 1.2 Update existing PmdJob tests in `processor/pmd_job_test.cc` for auto-return behavior
    - The `RegisterScheduleRunUnscheduleUnregister` test currently expects jobs to stay on `runner_jobs_` after `RunRunnableJobs()` — update it to expect jobs moving back to `pending_jobs_` with `kPending` state and `runner_size() == 0`
    - Remove the explicit `Unschedule()` step since auto-return already moves jobs to pending
    - Update the `RejectsInvalidTransitions` test if any assertions depend on jobs staying in `kRunner` after run
    - _Requirements: 1.1, 1.2, 1.3_

  - [x] 1.3 Add new unit tests for auto-return edge cases in `processor/pmd_job_test.cc`
    - Test: `RunRunnableJobs()` on empty runner list is a no-op (pending jobs unaffected)
    - Test: Single job auto-return cycle — schedule, run, verify kPending, re-schedule succeeds
    - Test: Multiple jobs all return to pending after single `RunRunnableJobs()` call
    - Test: Callback counter increments exactly once per `RunRunnableJobs()` invocation (Property 3)
    - Test: Re-schedule after auto-return succeeds and job runs again (Property 2)
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

- [x] 2. Checkpoint — PmdJobRunner auto-return
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. ListSlab `slab_base()` accessor and LRU infrastructure
  - [x] 3.1 Add `slab_base()` public accessor to `rxtx/list_slab.h`
    - Add `const uint8_t* slab_base() const { return slab_; }` to the public interface of `ListSlab`
    - _Requirements: 3.3_

  - [x] 3.2 Add `LruNode` struct and LRU list members to `rxtx/fast_lookup_table.h`
    - Define `struct LruNode` with `boost::intrusive::list_member_hook<> lru_hook` and `std::size_t slot_index`
    - Add `using LruList = boost::intrusive::list<LruNode, ...>` type alias
    - Add `std::unique_ptr<LruNode[]> lru_nodes_` parallel array member, allocated with same capacity as slab in constructor
    - Add `LruList lru_list_` member
    - Add private `SlotIndex(const LookupEntry*)` helper using `slab_.slab_base()` pointer arithmetic
    - _Requirements: 2.1, 2.2, 2.3, 2.4_

  - [x] 3.3 Integrate LRU operations into `Insert()`, `Find()`, `Remove()`, and `ForEach()` in `rxtx/fast_lookup_table.h`
    - `Insert()`: after inserting into hash set, compute slot index, set `lru_nodes_[slot].slot_index = slot`, push LruNode to LRU tail via `lru_list_.push_back()`
    - `Find()` (both overloads): on hit, compute slot index, splice corresponding LruNode to LRU tail
    - `Remove()`: before deallocating, unlink corresponding LruNode from LRU list
    - `ForEach()`: when `fn` returns true (erase), also unlink the corresponding LruNode
    - _Requirements: 2.5, 2.6, 3.1, 3.2, 3.3_

  - [x] 3.4 Implement `EvictLru(std::size_t batch_size)` method in `rxtx/fast_lookup_table.h`
    - Pop up to `batch_size` nodes from LRU head
    - For each popped node: compute `LookupEntry*` from `slot_index`, erase from hash set, deallocate from slab
    - Return count of entries actually removed
    - Handle empty list (return 0) and `batch_size > table size` (remove all)
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

  - [x] 3.5 Add LRU unit tests in `rxtx/fast_lookup_table_test.cc`
    - Test: Insert single entry → LRU list size equals 1 (Property 4 invariant)
    - Test: Insert N entries → LRU list size equals N
    - Test: Remove entry → LRU list size decreases (Property 4)
    - Test: Find hit promotes entry to LRU tail (Property 5)
    - Test: Find miss does not modify LRU order (Property 6)
    - Test: `EvictLru(0)` returns 0, table unchanged
    - Test: `EvictLru` on empty table returns 0 (Property 8)
    - Test: `EvictLru(batch_size)` removes from head in order (Property 7)
    - Test: `EvictLru` when table has fewer entries than batch_size removes all (Property 8)
    - Test: `ForEach` with removal correctly unlinks LruNodes (Property 4)
    - Test: Slot index computation matches expected pointer arithmetic
    - Test: `sizeof(LookupEntry) == 64` static assertion holds (Requirement 2.4)
    - _Requirements: 2.5, 2.6, 3.1, 3.2, 3.3, 4.1, 4.2, 4.4, 4.5, 4.6, 4.7_

- [x] 4. Checkpoint — LRU infrastructure
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. FiveTupleForwardingProcessor GC integration
  - [x] 5.1 Add `max_batch_count_` tracking and `kGcBatchSize` constant to `processor/five_tuple_forwarding_processor.h`
    - Add `static constexpr std::size_t kGcBatchSize = 16` constant
    - Add `uint16_t max_batch_count_ = 0` member variable
    - _Requirements: 5.1, 4.2_

  - [x] 5.2 Implement traffic tracking in `process_impl()` in `processor/five_tuple_forwarding_processor.cc`
    - Reset `max_batch_count_ = 0` at the top of `process_impl()`
    - After each `rte_eth_rx_burst`, update `max_batch_count_ = std::max(max_batch_count_, batch.Count())`
    - Move `RefreshGcScheduling()` call to after the RX loop (after `max_batch_count_` is fully computed)
    - Add code comment noting that some NIC PMD drivers (virtio, tap) may return fewer packets per burst even under load
    - _Requirements: 5.1, 5.2, 5.3, 6.5_

  - [x] 5.3 Implement `ShouldTriggerGc()` and update `RefreshGcScheduling()` in `processor/five_tuple_forwarding_processor.cc`
    - `ShouldTriggerGc()`: return `max_batch_count_ < kBatchSize / 2 && table_.size() >= table_.capacity() / 2`
    - `RefreshGcScheduling()`: sync `gc_job_scheduled_` with actual job state after auto-return by checking `flow_gc_job_.state() == PmdJob::State::kRunner`, then evaluate scheduling conditions
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 7.2_

  - [x] 5.4 Implement `RunFlowGc()` callback in `processor/five_tuple_forwarding_processor.cc`
    - Replace placeholder with `table_.EvictLru(kGcBatchSize)`
    - _Requirements: 7.1, 7.3_

  - [x] 5.5 Add GC scheduling unit tests in `processor/five_tuple_forwarding_processor_test.cc`
    - Test: `ShouldTriggerGc` returns true when `max_batch_count < kBatchSize / 2` AND `table.size() >= table.capacity() / 2` (Property 9)
    - Test: `ShouldTriggerGc` returns false when traffic is heavy (`max_batch_count >= kBatchSize / 2`)
    - Test: `ShouldTriggerGc` returns false when table is below 50% occupancy
    - Test: `ShouldTriggerGc` boundary values — `max_batch_count` at exactly `kBatchSize / 2`, table at exactly 50%
    - Note: These tests require exposing `ShouldTriggerGc()` and `max_batch_count_` for testing, or testing indirectly through `RefreshGcScheduling()` behavior
    - _Requirements: 5.2, 6.1, 6.2_

- [x] 6. Checkpoint — Update BUILD files if needed
  - Verify `rxtx/BUILD` has `@boost.intrusive` in `fast_lookup_table` deps (already present via `list_slab`)
  - Verify `processor/BUILD` deps are sufficient for the new code
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Final checkpoint
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- No RapidCheck / property-based tests — unit tests only (Google Test)
- Existing PmdJob tests in task 1.2 must be updated because auto-return changes the default RunRunnableJobs() behavior
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
