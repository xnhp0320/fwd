# Implementation Plan: IndirectTable

## Overview

Implement the IndirectTable two-layer indirection system as two composable C++ template components: `SlotArray<Value>` (flat array with reference counting and intrusive reverse hash table deduplication) and `IndirectTable<Key, Value>` (composes RcuHashTable + rte_mempool + SlotArray). All code lives under a new `indirect_table/` directory with Bazel build targets.

## Tasks

- [x] 1. Create directory structure, data models, and functor types
  - [x] 1.1 Create `indirect_table/` directory with `BUILD` file and header stubs
    - Create `indirect_table/value_slot.h` with the `ValueSlot<Value>` struct (alignas(64), IntrusiveRcuListHook, atomic refcount, Value)
    - Create `indirect_table/key_entry.h` with the `KeyEntry<Key>` struct (IntrusiveRcuListHook, Key, uint32_t value_id)
    - Create `indirect_table/functors.h` with `KeyEntryKeyExtractor<Key>` and `ValueSlotKeyExtractor<Value>`
    - Add `cc_library` targets for each header in `indirect_table/BUILD`
    - _Requirements: 9.1, 9.2, 15.1, 15.3_

- [x] 2. Implement SlotArray core
  - [x] 2.1 Create `indirect_table/slot_array.h` with full `SlotArray<Value, ValueHash, ValueEqual>` template
    - Implement `Init()`: rte_zmalloc for slots array, initialize free stack with IDs 0..capacity-1, construct ReverseMap
    - Implement destructor: rte_free the slots array and free stack
    - Implement `capacity()`, `used_count()`, `Get()` (direct array index)
    - Add `cc_library` target in BUILD
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 9.1, 9.2_

  - [x] 2.2 Implement SlotArray allocation and deduplication methods
    - Implement `FindOrAllocate()`: reverse map lookup, AddRef if found, else pop from free stack, write value, set refcount=1, insert into reverse map
    - Implement `Allocate()`: raw allocation without dedup, pop from free stack, set refcount=1
    - Implement `FindByValue()`: reverse map Find, return slot index or kInvalidId
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 5.1, 5.2, 5.3, 6.2, 6.3_

  - [x] 2.3 Implement SlotArray reference counting and deallocation
    - Implement `AddRef()`: assert id < capacity and refcount > 0, increment refcount
    - Implement `Release()`: assert id < capacity and refcount > 0, decrement refcount, return true if reaches 0
    - Implement `Deallocate()`: assert refcount == 0, remove from reverse map, push ID onto free stack
    - _Requirements: 3.1, 3.2, 3.3, 4.1, 4.2, 7.1, 7.2_

  - [x] 2.4 Implement SlotArray UpdateValue and ForEachInUse
    - Implement `UpdateValue()`: remove from reverse map, write new value, re-insert into reverse map
    - Implement `ForEachInUse()`: linear scan, invoke callback for slots with refcount > 0
    - Implement `RefCount()` accessor
    - _Requirements: 8.1, 8.2, 10.1_

  - [x] 2.5 Write unit tests for SlotArray
    - Create `indirect_table/slot_array_test.cc` with GTest
    - Test Init sets used_count=0 and capacity correctly
    - Test Allocate/Deallocate cycle, free stack exhaustion returns kInvalidId
    - Test FindOrAllocate dedup: same value returns same ID with incremented refcount
    - Test AddRef/Release lifecycle, Release returns true when refcount hits 0
    - Test FindByValue returns correct ID or kInvalidId
    - Test UpdateValue rehashes correctly in reverse map
    - Test ForEachInUse visits only in-use slots
    - Test Get returns valid pointer for allocated slots
    - Add `cc_test` target in BUILD with deps on slot_array, test_utils, gtest
    - _Requirements: 1.1–1.4, 2.1–2.5, 3.1–3.3, 4.1–4.2, 5.1–5.2, 6.1–6.3, 7.1–7.2, 8.1–8.2, 9.1–9.2, 10.1, 18.1, 18.5_

- [x] 3. Checkpoint — SlotArray complete
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement IndirectTable
  - [x] 4.1 Create `indirect_table/indirect_table.h` with full `IndirectTable<Key, Value, ...>` template
    - Implement `Init()`: initialize key RcuHashTable, create rte_mempool for KeyEntry allocation, initialize SlotArray, store RcuManager pointer
    - Implement destructor: free rte_mempool
    - Implement `slot_array()` accessors (const and non-const)
    - Add `cc_library` target in BUILD
    - _Requirements: 11.1, 11.2, 15.3_

  - [x] 4.2 Implement IndirectTable Insert and InsertWithId
    - Implement `Insert()`: check duplicate key, FindOrAllocate on SlotArray, allocate KeyEntry from mempool, populate and insert into key hash table; rollback refcount on mempool failure
    - Implement `InsertWithId()`: check duplicate key, AddRef on SlotArray, allocate KeyEntry from mempool, populate and insert; rollback AddRef on mempool failure
    - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 14.1, 14.2, 18.2, 18.3_

  - [x] 4.3 Implement IndirectTable Remove with RCU-safe reclamation
    - Implement `Remove()`: find KeyEntry, save value_id, unlink from hash table, schedule RetireViaGracePeriod with callback that returns KeyEntry to mempool and calls Release (then Deallocate if refcount reaches 0)
    - Handle non-existent key by returning false
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 14.1, 14.3, 18.4_

  - [x] 4.4 Implement IndirectTable lockless read path and utilities
    - Implement `Find()`, `Prefetch()`, `FindWithPrefetch()` delegating to key RcuHashTable
    - Implement `UpdateValue()` delegating to SlotArray
    - Implement `ForEachKey()` delegating to key RcuHashTable ForEach
    - _Requirements: 15.1, 15.2, 16.1, 17.1_

  - [x] 4.5 Write unit tests for IndirectTable
    - Create `indirect_table/indirect_table_test.cc` with GTest
    - Test Init succeeds and table is ready for operations
    - Test Insert/Find round-trip, duplicate key returns kInvalidId
    - Test many-to-one: multiple keys with same value share one slot (dedup)
    - Test InsertWithId attaches key to existing slot, increments refcount
    - Test Remove returns true for existing key, false for non-existent
    - Test mempool exhaustion rollback in Insert and InsertWithId
    - Test RCU retire callback: KeyEntry returned to mempool and refcount released after grace period
    - Test UpdateValue propagates to all keys sharing the slot
    - Test ForEachKey visits all inserted keys
    - Test Prefetch/FindWithPrefetch returns correct results
    - Add `cc_test` target in BUILD with deps on indirect_table, rcu_manager, rcu_retire, test_utils, gtest, dpdk
    - _Requirements: 11.1–11.2, 12.1–12.5, 13.1–13.4, 14.1–14.3, 15.1–15.2, 16.1, 17.1, 18.1–18.4_

- [x] 5. Checkpoint — IndirectTable complete
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. Property-based tests
  - [x] 6.1 Write property test for refcount invariant
    - **Property P1: Refcount Invariant** — After any random sequence of Insert/InsertWithId/Remove operations, verify that each slot's refcount equals the number of KeyEntries referencing it
    - **Validates: Requirements 3.1, 3.2, 3.3, 14.1, 14.2, 14.3**
    - Use RapidCheck to generate random operation sequences on IndirectTable

  - [x] 6.2 Write property test for value deduplication
    - **Property P2: Value Deduplication** — After any random sequence of FindOrAllocate calls, no two in-use slots contain the same value
    - **Validates: Requirements 5.1, 5.2**
    - Use RapidCheck to generate random value sequences on SlotArray

  - [ ]* 6.3 Write property test for reverse map consistency
    - **Property P3: Reverse Map Consistency** — After any operation, every in-use slot is findable via FindByValue, and FindByValue returns kInvalidId for values not in any slot
    - **Validates: Requirements 6.1, 6.2, 6.3**
    - Use RapidCheck to generate random mixed operations on SlotArray

  - [ ]* 6.4 Write property test for free stack consistency
    - **Property P5: Free Stack Consistency** — After any operation, free_top + used_count == capacity
    - **Validates: Requirements 7.1, 7.2**
    - Use RapidCheck to verify invariant after each operation in a random sequence

- [x] 7. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- The design uses C++ with DPDK, Abseil, and RapidCheck (all already in the project)
- SlotArray is implemented first as a standalone component, then composed into IndirectTable
- All mutations are control-plane only; PMD read path is lockless via direct array indexing and RcuHashTable
- Property tests use RapidCheck (already available via `@rapidcheck` Bazel dependency)
- Tests require DPDK EAL initialization via `rxtx::testing::InitEal()` for rte_zmalloc and rte_mempool
