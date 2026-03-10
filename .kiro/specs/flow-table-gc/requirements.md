# Requirements Document

## Introduction

This feature implements three related changes to the packet-processing data plane:

1. A PmdJobRunner refactor so that jobs automatically return to the pending list after `RunRunnableJobs()` executes them, requiring an explicit `Schedule()` call each cycle to run again.
2. An LRU-based garbage collection algorithm for `FastLookupTable` that evicts least-recently-used flow entries in bounded batches, using a parallel `LruNode` array indexed by slab slot with a `boost::intrusive::list` doubly-linked list.
3. Traffic-aware GC scheduling conditions in `FiveTupleForwardingProcessor` that schedule the GC job only when traffic is light AND the flow table is under pressure.

## Glossary

- **PmdJob**: A callback task managed by `PmdJobRunner`, with states `kIdle`, `kPending`, and `kRunner`. Defined in `processor/pmd_job.h`.
- **PmdJobRunner**: A single-threaded job scheduler that maintains `pending_jobs_` and `runner_jobs_` intrusive singly-linked lists. Defined in `processor/pmd_job.h`.
- **FastLookupTable**: A high-performance flow lookup table backed by `absl::flat_hash_set` of `LookupEntry` pointers, with slab allocation via `ListSlab`. Defined in `rxtx/fast_lookup_table.h`.
- **LookupEntry**: A 64-byte cache-line-aligned struct containing flow key fields and a session pointer. Defined in `rxtx/lookup_entry.h`.
- **ListSlab**: A slab allocator using `boost::intrusive::slist` free list, providing `Allocate`/`Deallocate` with contiguous backing storage. Defined in `rxtx/list_slab.h`.
- **LruNode**: A new struct containing a `boost::intrusive::list_member_hook<>` for the LRU doubly-linked list and an index back to the corresponding slab slot.
- **LRU_List**: A `boost::intrusive::list` of `LruNode` elements, ordered from least-recently-used (head) to most-recently-used (tail).
- **Batch**: A compile-time-sized array of `rte_mbuf*` pointers used for RX/TX burst operations. `kBatchSize` is 32 in `FiveTupleForwardingProcessor`. Defined in `rxtx/batch.h`.
- **GC_Job**: The `PmdJob` instance (`flow_gc_job_`) in `FiveTupleForwardingProcessor` whose callback invokes the flow table garbage collection routine.
- **Max_Batch_Count**: The maximum `batch.Count()` value observed across all RX queues within a single `process_impl()` invocation.
- **Slab_Slot_Index**: The zero-based index of a `LookupEntry` within the `ListSlab` contiguous backing array, computed as `(entry_address - slab_base) / sizeof(LookupEntry)`.

## Requirements

### Requirement 1: PmdJobRunner Auto-Return After Execution

**User Story:** As a PMD thread developer, I want jobs to automatically move back to the pending list after `RunRunnableJobs()` executes them, so that the caller must explicitly re-schedule a job each cycle to run it again.

#### Acceptance Criteria

1. WHEN `RunRunnableJobs(now_tsc)` completes, THE PmdJobRunner SHALL move every executed job from `runner_jobs_` to `pending_jobs_` and set each job's state to `kPending`.
2. WHEN `RunRunnableJobs(now_tsc)` completes, THE PmdJobRunner SHALL have zero jobs remaining in `runner_jobs_`.
3. WHEN a job is in `kPending` state after auto-return, THE PmdJobRunner SHALL accept a subsequent `Schedule()` call for that job.
4. WHEN `RunRunnableJobs(now_tsc)` is called with an empty `runner_jobs_` list, THE PmdJobRunner SHALL perform no state changes and return without error.
5. THE PmdJobRunner SHALL execute each job's callback exactly once per `RunRunnableJobs()` invocation before moving the job back to pending.

### Requirement 2: Parallel LruNode Array

**User Story:** As a data-plane developer, I want a parallel `LruNode` array indexed by slab slot, so that LRU tracking does not modify the 64-byte `LookupEntry` layout.

#### Acceptance Criteria

1. THE FastLookupTable SHALL allocate an `LruNode` array of the same capacity as the `ListSlab` backing array.
2. THE LruNode struct SHALL contain a `boost::intrusive::list_member_hook<>` member for the LRU doubly-linked list.
3. THE LruNode struct SHALL contain a `Slab_Slot_Index` field that identifies the corresponding `LookupEntry` in the slab.
4. THE LookupEntry struct SHALL remain exactly 64 bytes with no layout changes.
5. WHEN a new `LookupEntry` is inserted via `Insert()`, THE FastLookupTable SHALL add the corresponding `LruNode` to the tail of the LRU_List.
6. WHEN a `LookupEntry` is removed via `Remove()`, THE FastLookupTable SHALL unlink the corresponding `LruNode` from the LRU_List.

### Requirement 3: LRU Promotion on Lookup Hit

**User Story:** As a data-plane developer, I want lookup hits to promote the corresponding entry to the tail of the LRU list, so that actively used flows are protected from garbage collection.

#### Acceptance Criteria

1. WHEN `Find()` returns a non-null `LookupEntry` pointer, THE FastLookupTable SHALL move the corresponding `LruNode` from its current position to the tail of the LRU_List.
2. WHEN `Find()` returns nullptr, THE FastLookupTable SHALL not modify the LRU_List.
3. THE FastLookupTable SHALL compute the `Slab_Slot_Index` for a given `LookupEntry` pointer using pointer arithmetic: `(entry_address - slab_base) / entry_size`.

### Requirement 4: Bounded-Batch LRU Eviction

**User Story:** As a data-plane developer, I want the GC routine to evict a fixed number of least-recently-used entries per invocation, so that per-cycle GC cost is bounded and predictable.

#### Acceptance Criteria

1. WHEN the GC routine is invoked, THE FastLookupTable SHALL remove entries starting from the head of the LRU_List (least-recently-used first).
2. THE FastLookupTable SHALL remove at most `gc_batch_size` entries per GC invocation (configurable, default 16).
3. THE FastLookupTable SHALL unconditionally remove entries during GC without checking timestamps or age.
4. WHEN the LRU_List contains fewer entries than `gc_batch_size`, THE FastLookupTable SHALL remove only the entries present in the LRU_List.
5. WHEN the LRU_List is empty, THE FastLookupTable SHALL perform no removals and return zero.
6. THE FastLookupTable SHALL return the number of entries actually removed by the GC invocation.
7. FOR EACH removed entry, THE FastLookupTable SHALL deallocate the `LookupEntry` from the slab, erase the pointer from the hash set, and unlink the `LruNode` from the LRU_List.

### Requirement 5: Traffic Load Detection

**User Story:** As a processor developer, I want to track the maximum batch count across all RX queues per `process_impl()` call, so that the GC scheduler can distinguish light traffic from heavy traffic.

#### Acceptance Criteria

1. THE FiveTupleForwardingProcessor SHALL compute `Max_Batch_Count` as the maximum `batch.Count()` value across all RX queues processed in a single `process_impl()` invocation.
2. THE FiveTupleForwardingProcessor SHALL classify traffic as light WHEN `Max_Batch_Count` is less than `kBatchSize / 2` (i.e., less than 16 when `kBatchSize` is 32).
3. THE FiveTupleForwardingProcessor SHALL include a code comment noting that some NIC PMD drivers with limited burst sizes (e.g., virtio, tap) may return fewer packets per burst even under load, which can cause false light-traffic classification.

### Requirement 6: GC Scheduling Conditions

**User Story:** As a processor developer, I want the GC job to be scheduled only when traffic is light and the flow table is under pressure, so that GC work does not compete with packet processing during high-throughput periods.

#### Acceptance Criteria

1. THE FiveTupleForwardingProcessor SHALL schedule the GC_Job WHEN both of the following conditions are true: traffic is classified as light (Requirement 5, criterion 2) AND `table_.size() >= table_.capacity() / 2`.
2. THE FiveTupleForwardingProcessor SHALL unschedule the GC_Job WHEN traffic is classified as heavy (i.e., `Max_Batch_Count >= kBatchSize / 2`).
3. WHEN the GC_Job is already scheduled and conditions remain true, THE FiveTupleForwardingProcessor SHALL not re-schedule the GC_Job.
4. WHEN the GC_Job is already unscheduled and conditions remain false, THE FiveTupleForwardingProcessor SHALL not attempt to unschedule the GC_Job.
5. THE FiveTupleForwardingProcessor SHALL evaluate GC scheduling conditions at the beginning of each `process_impl()` invocation, after computing `Max_Batch_Count`.

### Requirement 7: GC Job Callback Integration

**User Story:** As a processor developer, I want the `RunFlowGc` callback to invoke the FastLookupTable's LRU eviction routine, so that stale flow entries are cleaned up when the GC job runs.

#### Acceptance Criteria

1. WHEN the GC_Job callback executes, THE FiveTupleForwardingProcessor SHALL call the FastLookupTable's bounded-batch LRU eviction method.
2. WHEN the PmdJobRunner auto-return moves the GC_Job back to pending after execution, THE FiveTupleForwardingProcessor SHALL re-evaluate scheduling conditions on the next `process_impl()` call to decide whether to re-schedule the GC_Job.
3. IF the GC_Job callback encounters an empty LRU_List, THEN THE FiveTupleForwardingProcessor SHALL complete the callback without error.
