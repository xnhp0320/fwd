# Implementation Plan: DPDK RX/TX Utility Classes

## Overview

Implement header-only `Packet` and `Batch` utility classes in `rxtx/` that provide zero-copy, cache-friendly abstractions over DPDK mbufs. The implementation proceeds bottom-up: Packet first (since Batch depends on it), then Batch with its iteration/filter/append methods, then test infrastructure with RapidCheck property-based tests. All code is C++ header-only with Bazel build targets.

## Tasks

- [x] 1. Set up rxtx directory structure and Bazel build targets
  - [x] 1.1 Create `rxtx/` directory with `BUILD` file defining header-only `cc_library` targets for `packet` and `batch`
    - Define `cc_library(name = "packet", hdrs = ["packet.h"], ...)` with `deps = ["//:dpdk_lib"]` and `visibility = ["//visibility:public"]`
    - Define `cc_library(name = "batch", hdrs = ["batch.h"], ...)` with `deps = ["//rxtx:packet"]` and `visibility = ["//visibility:public"]`
    - _Requirements: 1.1–1.8, 3.1–3.8_

  - [x] 1.2 Add RapidCheck dependency to `MODULE.bazel`
    - Add `bazel_dep(name = "rapidcheck", ...)` or an `http_archive` rule to fetch RapidCheck from GitHub if not in the Bazel Central Registry
    - _Requirements: Testing infrastructure for all properties_

- [x] 2. Implement Packet class
  - [x] 2.1 Create `rxtx/packet.h` with the Packet class
    - Define `kMbufStructSize`, `kCacheLineSize`, `kMetadataSize` constants
    - Implement `Packet::from(rte_mbuf*)` static method using placement new returning `Packet&`
    - Implement `Data()` (const and non-const), `Length()`, `Mbuf()` (const and non-const), `Free()` accessors
    - Delete default constructor, copy constructor, and copy assignment operator
    - Add `static_assert` verifying metadata region fits within `RTE_PKTMBUF_HEADROOM`
    - Private `rte_mbuf mbuf_` member as first field
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 2.1, 2.2, 2.3, 2.4_

  - [ ]* 2.2 Write property test: Packet address identity
    - **Property 1: Packet address identity**
    - Generate random mbuf-sized aligned memory, call `Packet::from`, verify `&pkt == mbuf` and `pkt.Mbuf() == mbuf`
    - **Validates: Requirements 1.1, 1.2, 1.8, 2.3**

  - [ ]* 2.3 Write property test: Packet accessor equivalence
    - **Property 2: Packet accessor equivalence**
    - Generate mbufs with random `data_off` and `data_len`, verify `Data()` matches `rte_pktmbuf_mtod` and `Length()` matches `rte_pktmbuf_data_len`
    - **Validates: Requirements 2.1, 2.2**

  - [ ]* 2.4 Write property test: Packet Free returns mbuf to pool
    - **Property 3: Packet Free returns mbuf to pool**
    - Allocate mbufs from a test pool, call `Free()`, verify pool available count increases
    - **Validates: Requirements 2.4**

  - [ ]* 2.5 Write unit tests for Packet class
    - Test type traits: `!is_default_constructible`, `!is_copy_constructible`, `!is_copy_assignable`
    - Test `kMetadataSize == 0`
    - Test `sizeof(rte_mbuf) == kMbufStructSize` runtime sanity check
    - _Requirements: 1.3, 1.5, 1.7_

- [x] 3. Checkpoint — Ensure Packet tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement Batch class template
  - [x] 4.1 Create `rxtx/batch.h` with the Batch class template
    - Define `Batch<uint16_t BatchSize, bool SafeMode = false>` template
    - Implement constructor initializing `count_` to 0
    - Implement destructor freeing all mbufs in `[0, count_)`
    - Implement `Data()`, `CountPtr()`, `Count()`, `SetCount()`, `Capacity()` accessors
    - Implement `Release()` method that sets count to 0 without freeing
    - Delete copy constructor and copy assignment operator
    - Private members: `rte_mbuf* mbufs_[BatchSize]` and `uint16_t count_`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 6.1, 6.2, 6.3_

  - [x] 4.2 Implement `ForEach` method on Batch
    - Template method accepting a callable `Fn`
    - Iterate `[0, count_)`, convert each `mbufs_[i]` to `Packet&` via `Packet::from`, invoke `fn(pkt)`
    - Return without invoking `fn` when `count_ == 0`
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 4.3 Implement `Filter` method on Batch
    - Template method accepting a callable `Fn`
    - Use write-index compaction: iterate `[0, count_)`, convert to `Packet&`, call predicate, compact retained mbufs to `[0, write)`
    - Update `count_` to number of retained packets
    - Do NOT free rejected mbufs
    - Return without invoking `fn` when `count_ == 0`
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7_

  - [x] 4.4 Implement `Append` methods on Batch with SafeMode toggle
    - `Append(rte_mbuf*)` and `Append(Packet&)` overloads
    - Use `if constexpr (SafeMode)` to select behavior at compile time
    - SafeMode=false: store at `mbufs_[count_++]`, return void, no bounds check
    - SafeMode=true: check `count_ >= BatchSize`, return false if full, otherwise store and return true
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 3.7, 3.8_

- [ ] 5. Implement test infrastructure and Batch property tests
  - [x] 5.1 Create mock mbuf test utilities for Packet and Batch tests
    - Implement aligned memory allocation for mock mbufs (e.g., `aligned_alloc` with 128-byte alignment)
    - Initialize mbuf fields needed by Packet accessors: `data_off`, `data_len`, `buf_addr`, `buf_len`
    - Implement a simple test allocator that tracks allocation/free counts for pool-count verification
    - Add Bazel `cc_test` targets for `rxtx/packet_test.cc` and `rxtx/batch_test.cc` in `rxtx/BUILD`
    - _Requirements: Testing infrastructure for Properties 1–10_

  - [ ]* 5.2 Write property test: Batch storage round-trip
    - **Property 4: Batch storage round-trip**
    - Generate random count `c` in `[0, N]`, fill `Data()` with `c` mock mbuf pointers, call `SetCount(c)`, verify `Count() == c` and each `Data()[i]` matches
    - **Validates: Requirements 3.2, 3.3, 3.4**

  - [ ]* 5.3 Write property test: ForEach visits all packets in order
    - **Property 5: ForEach visits all packets in order**
    - Generate batches of random size, record visited addresses in ForEach callback, verify they match `Data()[0..Count())` in order
    - **Validates: Requirements 4.2, 4.3**

  - [ ]* 5.4 Write property test: Filter correctness
    - **Property 6: Filter correctness**
    - Generate batches of random size and a random boolean predicate, call Filter, verify retained set matches predicate, contiguous layout, correct count, and no mbufs freed
    - **Validates: Requirements 5.2, 5.3, 5.4, 5.5, 5.6, 5.7**

  - [ ]* 5.5 Write property test: Batch destructor frees remaining mbufs
    - **Property 7: Batch destructor frees remaining mbufs**
    - Generate batches of random size, let them go out of scope, verify all mbufs returned to pool
    - **Validates: Requirements 6.1, 6.2**

  - [ ]* 5.6 Write property test: Batch Release prevents freeing
    - **Property 8: Batch Release prevents freeing**
    - Generate batches of random size, call `Release()`, verify `Count() == 0`, destroy batch and verify no additional frees
    - **Validates: Requirements 6.3**

  - [ ]* 5.7 Write property test: Batch Append correctness (unsafe mode)
    - **Property 9: Batch Append correctness (unsafe mode)**
    - Generate `Batch<N>` with random initial count `c < N`, append a mock mbuf, verify it appears at index `c`, `Count()` increments, and Append returns void (compile-time check). Verify `Append(Packet&)` equivalent to `Append(pkt.Mbuf())`
    - **Validates: Requirements 7.1, 7.2, 7.3, 7.4**

  - [ ]* 5.8 Write property test: Batch Append correctness (safe mode)
    - **Property 10: Batch Append correctness (safe mode)**
    - Generate `Batch<N, true>` with random initial count `c < N`, append a mock mbuf, verify it appears at index `c`, `Count()` increments, and Append returns true. Test full batch returns false and leaves `Count()` unchanged. Verify `Append(Packet&)` equivalent to `Append(pkt.Mbuf())`
    - **Validates: Requirements 7.1, 7.2, 7.3, 7.5**

  - [ ]* 5.9 Write unit tests for Batch class
    - Test type traits: `!is_copy_constructible`, `!is_copy_assignable`
    - Test `Batch<16>::Capacity() == 16`, `Batch<32>::Capacity() == 32`
    - Test ForEach on empty batch invokes callback zero times
    - Test Filter on empty batch invokes callback zero times
    - Test Filter does not free any mbufs (rejected mbufs remain accessible)
    - Test Append to empty batch sets `Count()` to 1 (both modes)
    - Test Append to full batch returns false (SafeMode=true)
    - Test Append return type is void in unsafe mode, bool in safe mode (static_assert)
    - Test destructor on empty batch is a no-op
    - _Requirements: 3.1, 3.6, 4.3, 5.4, 5.7, 7.4, 7.5, 6.2_

- [x] 6. Final checkpoint — Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Property tests use RapidCheck with a minimum of 100 iterations per property
- Mock mbuf utilities are shared between packet and batch test files
- All implementation is header-only in `rxtx/` directory
- Checkpoints ensure incremental validation after each major component
