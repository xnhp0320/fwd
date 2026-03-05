# Implementation Plan: Fast Lookup Table

## Overview

Implement a `ListSlab<Size, Allocator>` slab allocator and `FastLookupTable<Allocator>` hash table in the `rxtx` namespace. Build bottom-up: allocator policies first, then the slab, then the lookup entry, then the hash table, wiring everything together with BUILD targets. Each step is validated by unit and property-based tests using rapidcheck + googletest.

## Tasks

- [x] 1. Create allocator policies and slab infrastructure
  - [x] 1.1 Create `rxtx/allocator.h` with `StdAllocator` and `DpdkAllocator`
    - Define `StdAllocator` using aligned `operator new` / `operator delete`
    - Define `DpdkAllocator` using `rte_malloc` / `rte_free`
    - Both must provide `allocate(size_t bytes, size_t alignment)` and `deallocate(void* ptr)`
    - _Requirements: 4.1, 4.2, 4.3, 4.4_

  - [x] 1.2 Create `rxtx/list_slab.h` with `SlabNode` and `ListSlab<Size, Allocator>`
    - Define `SlabNode` with `boost::intrusive::slist_member_hook<> hook`
    - Implement `ListSlab` constructor: allocate contiguous block, placement-new `SlabNode` at each slot, push all onto `free_list_`
    - Implement destructor: clear free list, destroy nodes, deallocate block
    - Implement `Allocate<T>()`: pop from free list, return as `T*`; return `nullptr` if empty
    - Implement `Deallocate<T>(T*)`: push back onto free list
    - Add `static_assert` checks in `Allocate<T>` and `Deallocate<T>`: `sizeof(T) == Size`, `T::hook` is `slist_member_hook<>`, `offsetof(T, hook) == 0`
    - Implement `free_count()`, `used_count()`, `capacity()` accessors
    - Mark class non-copyable, non-movable
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.1, 2.2, 2.3, 2.4, 2.5, 3.1, 3.2, 3.3, 3.4, 4.1, 5.1, 5.2_

  - [x] 1.3 Update `rxtx/BUILD` with `allocator` and `list_slab` library targets
    - Add `cc_library` for `allocator` (header-only)
    - Add `cc_library` for `list_slab` with dep on `@boost.intrusive`
    - _Requirements: 3.1_

  - [x] 1.4 Add `boost.intrusive` dependency to `MODULE.bazel`
    - Add `bazel_dep(name = "boost.intrusive", ...)` if not already transitively available
    - Verify the slab compiles against the intrusive slist headers
    - _Requirements: 3.1_

- [x] 2. Implement ListSlab tests
  - [x] 2.1 Create `rxtx/list_slab_test.cc` with unit tests
    - Test construction: `free_count == capacity`, `used_count == 0` for capacities 1, 64, 1024
    - Test exhaustion: allocate all N entries, verify next `Allocate()` returns `nullptr`
    - Test deallocate and reuse: allocate, deallocate, allocate again — pointer is valid
    - Test capacity 0: all `Allocate()` calls return `nullptr`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.6, 2.1, 2.2, 2.3_

  - [ ]* 2.2 Write property test: slab count invariant
    - **Property 1: Slab count invariant**
    - Generate random sequences of allocate/deallocate operations; assert `free_count() + used_count() == capacity()` after every operation
    - **Validates: Requirements 1.1, 1.2, 1.3, 1.4**

  - [ ]* 2.3 Write property test: allocate/deallocate round trip
    - **Property 2: Allocate/deallocate round trip**
    - Allocate an entry, record counts, deallocate, assert counts restored
    - **Validates: Requirements 2.1, 2.3**

  - [x] 2.4 Add `list_slab_test` target to `rxtx/BUILD`
    - Add `cc_test` with deps on `list_slab`, `allocator`, `lookup_entry`, `googletest`, `rapidcheck`, `rapidcheck_gtest`
    - _Requirements: 1.1, 2.1_

- [x] 3. Checkpoint — Verify ListSlab builds and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement LookupEntry and hash/equality functors
  - [x] 4.1 Create `rxtx/lookup_entry.h` with `LookupEntry`, `LookupEntryHash`, `LookupEntryEq`
    - Define `LookupEntry` struct: `alignas(kCacheLineSize)`, fields: `hook`, `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `flags`, `vni`
    - Add `static_assert(sizeof(LookupEntry) == 64)` and `static_assert(alignof(LookupEntry) == 64)`
    - Implement `IsIpv6()` and `FromMetadata(const PacketMetadata&)`
    - Implement `LookupEntryHash`: dereference pointer, IPv4-optimized hashing using `absl::HashOf`
    - Implement `LookupEntryEq`: dereference pointers, compare flags first, then IPv4-optimized field comparison
    - Both functors must declare `using is_transparent = void`
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 7.3, 7.4, 8.1, 8.2, 8.3, 8.4, 8.5_

  - [x] 4.2 Add `lookup_entry` library target to `rxtx/BUILD`
    - Add `cc_library` with deps on `packet_metadata`, `packet`, `@boost.intrusive`, `@abseil-cpp//absl/hash`
    - _Requirements: 6.1_

- [x] 5. Implement LookupEntry and functor tests
  - [x] 5.1 Create `rxtx/fast_lookup_table_test.cc` with LookupEntry unit tests and property tests
    - Unit test: `FromMetadata` correctly maps fields from a known `PacketMetadata`
    - Unit test: `LookupEntryHash` produces same hash for two entries with identical key fields
    - Unit test: `LookupEntryEq` returns false for entries with different flags
    - _Requirements: 6.1, 6.2, 7.3, 7.4, 8.5_

  - [ ]* 5.2 Write property test: hash/equality consistency
    - **Property 3: Hash/equality consistency**
    - Generate two `LookupEntry` values with identical key fields; assert equal hashes and `LookupEntryEq` returns true
    - **Validates: Requirements 7.3, 7.4**

  - [ ]* 5.3 Write property test: IPv4 hash ignores v6 padding
    - **Property 4: IPv4 hash and equality ignore v6 padding**
    - Generate IPv4 `LookupEntry`, mutate `v6[4..15]` bytes, assert hash unchanged and equality holds
    - **Validates: Requirements 8.1, 8.3**

  - [ ]* 5.4 Write property test: IPv6 hash uses all 16 bytes
    - **Property 5: IPv6 hash and equality use all 16 bytes**
    - Generate IPv6 `LookupEntry`, flip a single byte in `v6`, assert not equal
    - **Validates: Requirements 8.2, 8.4**

  - [ ]* 5.5 Write property test: different flags means not equal
    - **Property 6: Different flags means not equal**
    - Generate two entries, one IPv4 one IPv6, assert `LookupEntryEq` returns false
    - **Validates: Requirements 8.5**

  - [ ]* 5.6 Write property test: FromMetadata round trip
    - **Property 11: FromMetadata round trip**
    - Generate random `PacketMetadata`, call `FromMetadata`, assert all key fields match and `flags == meta.flags & kFlagIpv6`
    - **Validates: Requirements 6.1, 6.2**

- [x] 6. Checkpoint — Verify LookupEntry and functors build and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement FastLookupTable
  - [x] 7.1 Create `rxtx/fast_lookup_table.h` with `FastLookupTable<Allocator>`
    - Define class with `ListSlab<sizeof(LookupEntry), Allocator> slab_` and `absl::flat_hash_set<LookupEntry*, LookupEntryHash, LookupEntryEq> set_`
    - Implement `Insert(src_ip, dst_ip, src_port, dst_port, protocol, vni, flags)`: stack probe, check existing, allocate from slab, insert pointer
    - Implement `Find(...)` and `Find(const PacketMetadata&)`: stack-allocated probe, `set_.find()`
    - Implement `Remove(LookupEntry*)`: erase from set, deallocate to slab
    - Implement static `FillEntry` helper
    - Implement `size()` and `capacity()` accessors
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 10.1, 10.2_

  - [x] 7.2 Add `fast_lookup_table` library target to `rxtx/BUILD`
    - Add `cc_library` with deps on `list_slab`, `lookup_entry`, `allocator`, `@abseil-cpp//absl/container:flat_hash_set`, `@abseil-cpp//absl/hash`
    - _Requirements: 7.1_

- [x] 8. Implement FastLookupTable tests
  - [x] 8.1 Add FastLookupTable unit tests to `rxtx/fast_lookup_table_test.cc`
    - Test empty find returns `nullptr`
    - Test remove on empty table returns `false`
    - Test insert then find returns matching entry
    - Test duplicate insert returns same pointer, size increments only once
    - Test capacity exhaustion: fill to capacity, next insert with new key returns `nullptr`
    - Test remove then find returns `nullptr`
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.6_

  - [ ]* 8.2 Write property test: insert/find round trip
    - **Property 7: Insert/find round trip**
    - Generate random 5-tuple + VNI + flags, insert, find with same key, assert non-null and fields match
    - **Validates: Requirements 9.1, 9.4**

  - [ ]* 8.3 Write property test: duplicate insert idempotence
    - **Property 8: Duplicate insert idempotence**
    - Insert same key twice, assert same pointer returned and `size()` increased by 1
    - **Validates: Requirements 9.3**

  - [ ]* 8.4 Write property test: remove then find returns null
    - **Property 9: Remove then find returns null**
    - Insert, remove, find — assert `nullptr` and `size()` decreased
    - **Validates: Requirements 9.5**

  - [ ]* 8.5 Write property test: insert/remove slab balance
    - **Property 10: Insert/remove slab balance**
    - Insert N distinct keys, remove all N, assert slab `free_count()` equals capacity
    - **Validates: Requirements 2.1, 2.3, 9.1, 9.5**

  - [x] 8.6 Add `fast_lookup_table_test` target to `rxtx/BUILD`
    - Add `cc_test` with deps on `fast_lookup_table`, `lookup_entry`, `allocator`, `packet_metadata`, `googletest`, `rapidcheck`, `rapidcheck_gtest`
    - _Requirements: 7.1, 9.1_

- [x] 9. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 10. Implement Modification Toggle
  - [x] 10.1 Add `std::atomic<bool> modifiable_` member to `FastLookupTable` (default `true`)
    - Add a `std::atomic<bool> modifiable_{true}` private member to the `FastLookupTable` class template
    - `SetModifiable` will be called from the ControlPlane thread; reads use `memory_order_relaxed`
    - _Requirements: 11.1, 11.7, 11.8_

  - [x] 10.2 Add `SetModifiable(bool)` and `IsModifiable()` methods
    - Implement `void SetModifiable(bool m) { modifiable_.store(m, std::memory_order_release); }` and `bool IsModifiable() const { return modifiable_.load(std::memory_order_acquire); }`
    - Release store pairs with acquire load: ControlPlane sets flag, then enqueues deferred work via `call_after_grace_period`; PMD thread reads flag with acquire in Insert/Remove
    - _Requirements: 11.3, 11.4, 11.8, 11.9_

  - [x] 10.3 Add `modifiable_` guard to `Insert()` — return `nullptr` if false
    - Add `if (!modifiable_.load(std::memory_order_acquire)) return nullptr;` at the top of `Insert()`
    - _Requirements: 11.5, 11.9_

  - [x] 10.4 Add `modifiable_` guard to `Remove()` — return `false` if false
    - Add `if (!modifiable_.load(std::memory_order_acquire)) return false;` at the top of `Remove()`
    - _Requirements: 11.6, 11.9_

  - [x] 10.5 Initialize `modifiable_` to `true` in constructor
    - The `std::atomic<bool> modifiable_{true}` in-class initializer handles this; verify constructor doesn't override it
    - _Requirements: 11.2_

- [x] 11. Implement User-Controlled Iteration API
  - [x] 11.1 Add `using Iterator = Set::iterator` type alias to `FastLookupTable`
    - Expose the underlying `absl::flat_hash_set` iterator as a public type alias
    - _Requirements: 12.1, 12.2_

  - [x] 11.2 Add `Begin()` and `End()` methods
    - Implement `Iterator Begin() { return set_.begin(); }` and `Iterator End() { return set_.end(); }`
    - Non-const because the returned iterators may be used with non-const `ForEach` for erase-during-iteration
    - _Requirements: 12.3, 12.4_

  - [x] 11.3 Implement `template<typename Fn> size_t ForEach(Iterator& it, size_t count, Fn fn)`
    - Non-const method. Loop up to `count` times while `it != set_.end()`:
      - Call `fn(*it)` which returns bool
      - If fn returns true: erase entry via `it = set_.erase(it)`, deallocate to slab
      - If fn returns false: advance `it` normally
    - Return number of entries visited
    - _Requirements: 12.5, 12.6, 12.7, 12.8, 12.9_

  - [x] 11.4 Add documentation comment about iterator invalidation on Insert/Remove
    - Document in the class comment that iterators follow standard `absl::flat_hash_set` invalidation semantics and the caller must not use invalidated iterators
    - _Requirements: 12.7, 12.8_

- [x] 12. Implement Modification Toggle tests
  - [x] 12.1 Add unit tests for modification toggle to `rxtx/fast_lookup_table_test.cc`
    - Test: modifiable defaults to true — construct table, assert `IsModifiable() == true`
    - Test: SetModifiable round trip — set false, assert false; set true, assert true
    - Test: Insert blocked when not modifiable — set false, call Insert, assert `nullptr` and `size()` unchanged
    - Test: Remove blocked when not modifiable — insert entry, set false, call Remove, assert `false` and `size()` unchanged
    - Test: Re-enable modifications — set false then true, verify Insert and Remove work normally
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.7_

  - [ ]* 12.2 Write property test: modification toggle blocks Insert and Remove
    - **Property 12: Modification toggle blocks Insert and Remove**
    - Generate random 5-tuple + VNI + flags, set `modifiable_` to false, assert `Insert` returns `nullptr` and `Remove` returns `false` without changing `size()`
    - **Validates: Requirements 11.5, 11.6**

- [x] 13. Implement Iteration API tests
  - [x] 13.1 Add unit tests for iteration API to `rxtx/fast_lookup_table_test.cc`
    - Test: ForEach on empty table returns 0 — call `ForEach` from `Begin()` with count 10 and fn returning false, assert returns 0 and iterator equals `End()`
    - Test: ForEach with count 0 returns 0 — insert entries, call `ForEach` with count 0, assert returns 0 and iterator unchanged
    - Test: ForEach visits all entries — insert N entries, call `ForEach` from `Begin()` with count N and fn returning false, assert returns N and iterator equals `End()`
    - Test: ForEach partial iteration — insert N entries, call `ForEach` with count < N and fn returning false, assert returns count and iterator is not at `End()`
    - Test: ForEach with removal — insert N entries, call `ForEach` with fn returning true, assert `size()` is 0 and slab `free_count` equals capacity
    - Test: ForEach selective removal — insert N entries, fn returns true for some and false for others, assert `size()` decreases correctly
    - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8, 12.9_

  - [ ]* 13.2 Write property test: ForEach visits all entries in a single pass
    - **Property 13: ForEach visits all entries in a single pass**
    - Insert N random entries, call `ForEach` from `Begin()` with `count >= N`, assert returns N and iterator equals `End()`
    - **Validates: Requirements 12.5, 12.6**

  - [ ]* 13.3 Write property test: chunked ForEach visits all entries
    - **Property 14: Chunked ForEach visits all entries**
    - Insert N random entries, call `ForEach` repeatedly with arbitrary positive chunk sizes and fn returning false, assert sum of visited equals `size()` when iterator reaches `End()`
    - **Validates: Requirements 12.5, 12.8**

  - [ ]* 13.4 Write property test: ForEach with removal reclaims slab entries
    - **Property 15: ForEach with removal reclaims slab entries**
    - Insert N random entries, call `ForEach` with fn returning true for all, assert `size()` is 0 and slab `free_count` equals capacity
    - **Validates: Requirements 12.6, 12.7**

- [x] 14. Final checkpoint — Verify all new tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- All headers are header-only templates; no `.cc` files needed for the libraries
- Property tests use rapidcheck with `RC_GTEST_PROP` macros, matching the existing `packet_metadata_test.cc` pattern
- `StdAllocator` is used in all tests; `DpdkAllocator` requires DPDK EAL and is not tested here
