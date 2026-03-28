# Implementation Plan: F14 Lookup Table

## Overview

Port the C-based F14 hash map to a modern C++20 template implementation across three header-only files (`f14_simd.h`, `f14_map.h`, `f14_lookup_table.h`) with cross-platform SIMD support (SSE2, NEON, scalar fallback), property-based tests via RapidCheck, and BUILD target integration.

## Tasks

- [x] 1. Implement SIMD abstraction layer (`rxtx/f14_simd.h`)
  - [x] 1.1 Create `rxtx/f14_simd.h` with constants, `Sse2Backend`, `NeonBackend`, `ScalarBackend`, and compile-time `SimdBackend` alias
    - Define `TagMask`, `kFullMask`, `kCapacity`, `kDesiredCapacity` constants
    - Implement `ScalarBackend::TagMatch` and `ScalarBackend::OccupiedMask` with byte-by-byte loop
    - Implement `Sse2Backend::TagMatch` using `_mm_load_si128`, `_mm_set1_epi8`, `_mm_cmpeq_epi8`, `_mm_movemask_epi8`
    - Implement `Sse2Backend::OccupiedMask` using `_mm_load_si128`, `_mm_movemask_epi8`
    - Implement `NeonBackend::TagMatch` using `vld1q_u8`, `vdupq_n_u8`, `vceqq_u8`, `vshrn_n_u16` bitmask extraction
    - Implement `NeonBackend::OccupiedMask` using NEON intrinsics
    - Add preprocessor guards for `__SSE2__`, `__ARM_NEON`, scalar fallback
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_

  - [x] 1.2 Write property test: SIMD tag matching correctness (Property 1)
    - **Property 1: SIMD tag matching correctness**
    - Generate random 16-byte aligned `ChunkHeader` buffers and random needle bytes
    - Verify `SimdBackend::TagMatch` returns correct bitmask (bit N set iff `header[N] == needle`, bits 14-15 zero)
    - Verify `SimdBackend::OccupiedMask` returns correct bitmask (bit N set iff `header[N] != 0`, bits 14-15 zero)
    - **Validates: Requirements 1.1, 1.2, 1.7**

  - [x] 1.3 Write property test: SIMD cross-platform equivalence (Property 2)
    - **Property 2: SIMD cross-platform equivalence**
    - For random headers and needles, verify `SimdBackend::TagMatch` equals `ScalarBackend::TagMatch`
    - Verify `SimdBackend::OccupiedMask` equals `ScalarBackend::OccupiedMask`
    - **Validates: Requirements 8.5**

  - [x] 1.4 Write property test: Tag derivation always non-zero (Property 3)
    - **Property 3: Tag derivation always non-zero**
    - For random `std::size_t` hash values, verify `SplitHash(hash).tag` has bit 7 set (`tag >= 0x80`)
    - **Validates: Requirements 2.10, 5.6**

- [x] 2. Implement F14Map core (`rxtx/f14_map.h`)
  - [x] 2.1 Create `rxtx/f14_map.h` with `ChunkHeader`, `Chunk<Item>`, `HashPair`, `SplitHash`, `ProbeDelta`, `PackedPtr`, `DefaultChunkAllocator`
    - `ChunkHeader`: 16-byte aligned struct with `tags[14]`, `control`, `overflow`
    - `Chunk<Item>`: 128-byte aligned struct wrapping `ChunkHeader` + `items[14]`, with tag/overflow/SIMD accessors
    - `HashPair`, `SplitHash`, `ProbeDelta`: hash splitting and probe delta computation
    - `PackedPtr`, `PackedFromItemPtr`: packed pointer encoding matching C fmap's `fmap_packed_ptr`
    - Add `static_assert` for `sizeof(ChunkHeader) == 16`, `alignof(ChunkHeader) == 16`, `sizeof(Chunk<void*>) == 128`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 2.10, 9.3_

  - [x] 2.2 Implement `F14Map` class template with Find, Insert, Erase, Clear, ForEach, size()
    - Template parameters: `Key`, `Value`, `Hash`, `KeyEqual`, `Allocator`, `EnableItemIteration`
    - Constructor with optional initial capacity, destructor deallocating chunks
    - `Find`: probe sequence with SIMD TagMatch, overflow count check, return `Value*` or nullptr
    - `Insert`: find-or-insert with `ReserveForInsert`, probe for empty slot, overflow counting
    - `Erase`: clear tag, walk probe chain to decrement overflow/hosted-overflow counts
    - `Clear`: reset all chunks or deallocate
    - `ForEach`: iterate occupied items via chunk scanning, support erasure callback
    - `ReserveForInsert` / `Rehash`: growth factor ~1.41×, power-of-two chunk count
    - Replace OVS dependencies: `[[likely]]`/`[[unlikely]]` for branch hints, `__builtin_prefetch`, standard allocation
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.11, 5.1, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [x] 2.3 Implement `ItemIterator` with `Advance`, `AdvancePrechecked`, `AdvanceLikelyDead`, and `Begin()`/`End()` methods
    - Conditional compilation via `if constexpr` on `EnableItemIteration`
    - `AdjPackedBeginAfterInsert` and `AdjPackedBeginBeforeErase` for packed_begin tracking
    - `[[no_unique_address]]` for zero-overhead when iteration disabled
    - Three advance variants matching C fmap semantics
    - _Requirements: 9.1, 9.2, 9.4, 9.5, 9.6, 9.7, 9.8, 9.9_

  - [x] 2.4 Write property test: Capacity computation produces valid power-of-two (Property 4)
    - **Property 4: Capacity computation produces valid power-of-two**
    - For random desired capacities in [1, 10000], verify chunk_count is power of two and `chunk_count * kDesiredCapacity >= desired`
    - **Validates: Requirements 2.9**

  - [x] 2.5 Write property test: F14Map model-based correctness (Property 5)
    - **Property 5: F14Map model-based correctness**
    - Generate random sequences of insert/erase/find with integer keys against both F14Map and `std::unordered_map`
    - After each operation verify: (a) sizes equal, (b) all reference keys found with correct value, (c) absent keys not found, (d) occupied slot sum equals size, (e) hosted overflow sum equals non-home item count
    - **Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.6, 8.7, 8.9, 2.4**

  - [x] 2.6 Write property test: PackedPtr round-trip encoding (Property 9)
    - **Property 9: PackedPtr round-trip encoding**
    - For random aligned item pointers and indices in [0, 13], verify encode-then-decode produces original pointer and index
    - **Validates: Requirements 9.3**

  - [x] 2.7 Write property test: Item iteration visits exactly size() items (Property 10)
    - **Property 10: Item iteration visits exactly size() items**
    - After random insert/erase sequences on `F14Map<..., true>`, iterate Begin() to End() and verify exactly `size()` items visited, each once
    - Verify all three advance variants produce equivalent traversals
    - **Validates: Requirements 9.2, 9.4, 9.5, 9.10**

- [x] 3. Checkpoint - Verify SIMD and F14Map
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement F14LookupTable (`rxtx/f14_lookup_table.h`)
  - [x] 4.1 Create `rxtx/f14_lookup_table.h` with `F14LookupTable<Allocator>` class
    - Compose `F14Map<LookupEntry*, LookupEntry*, LookupEntryHash, LookupEntryEq, DefaultChunkAllocator, false>` with `ListSlab<64>`, `LruNode[]`, `LruList`
    - Implement `Insert`: check modifiable, probe for existing, allocate from slab, insert into map, add to LRU tail
    - Implement `Find` (two overloads): stack probe, map lookup, LRU promotion on hit
    - Implement `Remove`: check modifiable, erase from map, remove from LRU, deallocate to slab
    - Implement `ForEach`: iterate via LRU list, support erasure callback
    - Implement `EvictLru`: pop from LRU head, erase from map, deallocate to slab
    - Implement `SetModifiable`/`IsModifiable` with `std::atomic<bool>` release/acquire
    - Implement `size()`, `capacity()` accessors
    - Handle error cases: slab full returns nullptr, modifiable=false blocks Insert/Remove, duplicate insert returns existing
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 4.10, 4.11, 4.12_

  - [x] 4.2 Write property test: F14LookupTable insert-find round-trip (Property 6)
    - **Property 6: F14LookupTable insert-find round-trip**
    - For random flow keys, insert then verify both Find overloads return matching entry
    - **Validates: Requirements 4.1, 4.2, 4.3, 8.2**

  - [x] 4.3 Write property test: F14LookupTable LRU promotion on Find hit (Property 7)
    - **Property 7: F14LookupTable LRU promotion on Find hit**
    - Insert N entries, Find one, verify it is last evicted by repeated `EvictLru(1)`
    - **Validates: Requirements 4.7**

  - [x] 4.4 Write property test: F14LookupTable behavioral equivalence with FastLookupTable (Property 8)
    - **Property 8: F14LookupTable behavioral equivalence with FastLookupTable**
    - Apply same random operation sequences to both F14LookupTable and FastLookupTable
    - Verify Insert/Find/Remove/EvictLru produce identical observable results and sizes match
    - **Validates: Requirements 8.8, 4.8, 4.11**

  - [x] 4.5 Write unit tests for edge cases
    - Slab exhaustion: fill to capacity, verify next Insert returns nullptr
    - Modifiable flag: Insert/Remove blocked when false, unblocked when re-enabled
    - Empty table: Find returns nullptr, Remove returns false, EvictLru returns 0
    - Duplicate insert: same pointer returned, size unchanged
    - Overflow saturation: increment from 255 stays at 255, decrement from 255 stays at 255
    - `EnableItemIteration=false` compiled-out check: `sizeof(F14Map<..., false>)` < `sizeof(F14Map<..., true>)`
    - Begin()/End() on empty map with `EnableItemIteration=true`: Begin() equals End()
    - _Requirements: 4.8, 4.11, 4.12, 3.7, 9.1, 9.6_

- [x] 5. Update BUILD targets (`rxtx/BUILD`)
  - [x] 5.1 Add `cc_library` targets for `f14_simd`, `f14_map`, `f14_lookup_table`
    - `f14_simd`: header-only, no deps beyond standard headers
    - `f14_map`: depends on `f14_simd`
    - `f14_lookup_table`: depends on `f14_map`, `list_slab`, `lookup_entry`, `allocator`, `packet_metadata`, `@boost.intrusive`
    - _Requirements: 6.1_

  - [x] 5.2 Add `cc_test` targets for `f14_simd_test`, `f14_map_test`, `f14_lookup_table_test`
    - All depend on `@googletest//:gtest`, `@rapidcheck`, `@rapidcheck//:rapidcheck_gtest`
    - `f14_lookup_table_test` also depends on `fast_lookup_table` for behavioral equivalence test (Property 8)
    - _Requirements: 8.1 through 8.9_

- [x] 6. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties via RapidCheck
- Unit tests validate specific examples and edge cases
- All three header files are header-only templates matching existing project patterns
- The implementation language is C++20 throughout
