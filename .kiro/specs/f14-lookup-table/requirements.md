# Requirements Document

## Introduction

This document specifies the requirements for porting the existing C-based F14 hash map implementation (`processor/fmap.h`, `processor/fmap-detail.h`, `processor/fmap.c`) to a modern C++ template-based implementation with cross-platform SIMD support. The new implementation adds ARM NEON intrinsics alongside the existing x86-64 SSE2 support, replaces function-pointer dispatch with compile-time template parameters, removes external OVS dependencies, and integrates as a drop-in alternative backend for the existing `FastLookupTable` in `rxtx/fast_lookup_table.h`.

The C fmap is an implementation of Meta's Folly F14 algorithm: a 14-way probing hash table where each chunk stores 14 one-byte tag fingerprints in a 16-byte SIMD-aligned header, enabling parallel tag matching via SIMD comparison and bitmask extraction. The current implementation is x86-64 only and uses `struct fmap_ops` function pointers for hash, equality, allocation, and key extraction, which prevents inlining.

## Glossary

- **F14_Map**: The new C++ template class implementing the F14 hash map algorithm, parameterized by Key, Value, Hash, KeyEqual, and Allocator.
- **Chunk**: A 128-byte (2 cache lines) data structure containing a 16-byte SIMD-aligned header (14 tag bytes + control byte + overflow byte) followed by 14 item slots.
- **Tag**: A 1-byte hash fingerprint stored in the chunk header, derived from the upper bits of the hash value. A tag value of 0 indicates an empty slot.
- **Chunk_Header**: The 16-byte aligned structure within each Chunk containing 14 tag bytes, a control byte (scale in low nibble, hosted overflow count in high nibble), and an overflow byte.
- **SIMD_Backend**: A compile-time abstraction over platform-specific SIMD intrinsics (SSE2 on x86-64, NEON on aarch64) used for parallel tag matching and occupied-mask extraction within a Chunk_Header.
- **Tag_Match**: The SIMD operation that compares all 14 tag bytes in a Chunk_Header against a needle byte simultaneously, producing a bitmask of matching positions.
- **Occupied_Mask**: The SIMD operation that produces a bitmask indicating which of the 14 slots in a Chunk_Header contain non-zero tags.
- **Double_Hashing**: The probing strategy where the probe step (delta) is computed as `2 * tag + 1`, ensuring the step is always odd and thus coprime with any power-of-two chunk count.
- **Overflow_Count**: A per-chunk reference counter tracking how many items that hash to this chunk have overflowed to subsequent chunks. Enables efficient erase without tombstones.
- **Hosted_Overflow_Count**: A per-chunk counter (stored in the high nibble of the control byte) tracking how many overflow items from other chunks are hosted in this chunk.
- **F14_Lookup_Table**: The new C++ class that wraps F14_Map and provides the same external API as the existing `FastLookupTable`, including LRU tracking, slab allocation, and the `SetModifiable` atomic flag.
- **FastLookupTable**: The existing abseil-backed lookup table in `rxtx/fast_lookup_table.h` that uses `absl::flat_hash_set<LookupEntry*>`.
- **LookupEntry**: A 64-byte cache-aligned struct defined in `rxtx/lookup_entry.h` containing flow key fields and a session pointer.
- **Scalar_Fallback**: A portable non-SIMD implementation of tag matching using byte-by-byte comparison, used on platforms without SSE2 or NEON support.

## Requirements

### Requirement 1: SIMD Abstraction Layer

**User Story:** As a developer, I want a compile-time SIMD abstraction layer, so that the F14 tag matching operations work efficiently on both x86-64 (SSE2) and ARM aarch64 (NEON) platforms without code duplication.

#### Acceptance Criteria

1. THE SIMD_Backend SHALL provide a `TagMatch(header_ptr, needle)` operation that compares all 14 tag bytes in a Chunk_Header against a single needle byte and returns a bitmask of matching positions.
2. THE SIMD_Backend SHALL provide an `OccupiedMask(header_ptr)` operation that returns a bitmask indicating which of the 14 slots contain non-zero tags.
3. WHEN compiled on x86-64, THE SIMD_Backend SHALL use SSE2 intrinsics (`_mm_load_si128`, `_mm_set1_epi8`, `_mm_cmpeq_epi8`, `_mm_movemask_epi8`) for TagMatch and OccupiedMask.
4. WHEN compiled on aarch64, THE SIMD_Backend SHALL use NEON intrinsics (`vld1q_u8`, `vdupq_n_u8`, `vceqq_u8`, and a bitmask extraction sequence) for TagMatch and OccupiedMask.
5. WHEN compiled on a platform without SSE2 or NEON support, THE SIMD_Backend SHALL provide a Scalar_Fallback that iterates over the 14 tag bytes individually.
6. THE SIMD_Backend SHALL be selected at compile time via preprocessor detection (`__x86_64__` / `__SSE2__` for SSE2, `__aarch64__` / `__ARM_NEON` for NEON), with no runtime dispatch overhead.
7. THE SIMD_Backend SHALL operate on 16-byte aligned Chunk_Header data and mask results to the low 14 bits (FMAP_FULL_MASK = 0x3FFF) to exclude the control and overflow bytes.

### Requirement 2: C++ F14 Map Core

**User Story:** As a developer, I want a C++ template-based F14 hash map, so that hash, equality, and allocation operations are inlined by the compiler instead of dispatched through function pointers.

#### Acceptance Criteria

1. THE F14_Map SHALL be a C++ class template parameterized by `Key`, `Value`, `Hash`, `KeyEqual`, and `Allocator`.
2. THE F14_Map SHALL store items in Chunk structures identical in layout to the C fmap: 128 bytes per chunk with a 16-byte Chunk_Header followed by 14 item slots.
3. THE F14_Map SHALL use Double_Hashing with probe delta `2 * tag + 1` for collision resolution, matching the C fmap algorithm.
4. THE F14_Map SHALL use reference-counted Overflow_Count and Hosted_Overflow_Count per chunk for erase operations, avoiding tombstones.
5. THE F14_Map SHALL use the SIMD_Backend for TagMatch and OccupiedMask operations within the find, insert, and erase hot paths.
6. THE F14_Map SHALL invoke the Hash and KeyEqual template parameters as direct function calls, enabling the compiler to inline these operations.
7. THE F14_Map SHALL support `find(key)`, `insert(key, value)`, `erase(key)`, `size()`, and `clear()` operations.
8. THE F14_Map SHALL automatically rehash when the load factor exceeds the capacity threshold (chunk_count × 12), growing by a factor between 2^0.5 and 2^1.5 as in the C fmap.
9. THE F14_Map SHALL compute chunk_count as the next power of two from `ceil(desired_size / 12)`, matching the C fmap scaling strategy.
10. THE F14_Map SHALL derive the tag from the hash value as `(hash >> 24) | 0x80`, ensuring the tag is always non-zero.
11. THE F14_Map SHALL have no dependencies on OVS utilities (`util.h`, `ovs-atomic.h`, `dynamic-string.h`).

### Requirement 3: Chunk Data Structure

**User Story:** As a developer, I want the C++ chunk structure to be binary-compatible with the C fmap chunk layout, so that the F14 algorithm operates correctly with SIMD alignment requirements.

#### Acceptance Criteria

1. THE Chunk SHALL be exactly 128 bytes: a 16-byte Chunk_Header followed by 14 item slots.
2. THE Chunk_Header SHALL be aligned to a 16-byte boundary for SIMD load operations.
3. THE Chunk_Header SHALL contain 14 tag bytes at offsets 0-13, a control byte at offset 14, and an overflow byte at offset 15.
4. THE Chunk SHALL store the scale (desired capacity per chunk) in the low nibble of the control byte of chunk index 0.
5. THE Chunk SHALL store the Hosted_Overflow_Count in the high nibble of the control byte.
6. WHEN a tag byte is 0, THE Chunk SHALL treat that slot as empty.
7. WHEN the overflow byte reaches 255, THE Chunk SHALL saturate the Overflow_Count at 255 without further increment or decrement.

### Requirement 4: F14 Lookup Table Integration

**User Story:** As a developer, I want an F14-backed lookup table that provides the same API as the existing FastLookupTable, so that it can serve as a drop-in alternative backend.

#### Acceptance Criteria

1. THE F14_Lookup_Table SHALL provide an `Insert(src_ip, dst_ip, src_port, dst_port, protocol, vni, flags)` method that allocates a LookupEntry from the slab, inserts the entry into the F14_Map, and returns a pointer to the entry.
2. THE F14_Lookup_Table SHALL provide a `Find(src_ip, dst_ip, src_port, dst_port, protocol, vni, flags)` method that constructs a stack-allocated probe LookupEntry and returns a pointer to the matching slab entry, or nullptr if not found.
3. THE F14_Lookup_Table SHALL provide a `Find(PacketMetadata)` overload that populates the probe from PacketMetadata fields.
4. THE F14_Lookup_Table SHALL provide a `Remove(LookupEntry*)` method that erases the entry from the F14_Map and returns the entry to the slab.
5. THE F14_Lookup_Table SHALL provide a `ForEach(iterator, count, fn)` method that visits up to count entries, erasing entries for which fn returns true.
6. THE F14_Lookup_Table SHALL provide an `EvictLru(batch_size)` method that removes up to batch_size least-recently-used entries from the table.
7. THE F14_Lookup_Table SHALL maintain an LRU doubly-linked list using boost::intrusive::list with a parallel LruNode array, promoting entries to the tail on Find hits.
8. THE F14_Lookup_Table SHALL provide `SetModifiable(bool)` and `IsModifiable()` methods using std::atomic<bool> with release/acquire ordering, returning nullptr or false from Insert and Remove when modifiable is false.
9. THE F14_Lookup_Table SHALL provide `size()` and `capacity()` accessors returning the current entry count and slab capacity.
10. THE F14_Lookup_Table SHALL use ListSlab<sizeof(LookupEntry)> for entry allocation, matching the existing FastLookupTable slab strategy.
11. WHEN Insert is called with a key that already exists, THE F14_Lookup_Table SHALL return the pointer to the existing entry without inserting a duplicate.
12. WHEN Insert is called and the slab is full and the key does not exist, THE F14_Lookup_Table SHALL return nullptr.

### Requirement 5: Template-Based Function Pointer Replacement

**User Story:** As a developer, I want the C fmap function-pointer dispatch (`struct fmap_ops`) replaced with C++ template parameters, so that the compiler can inline hash, equality, and allocation operations in the hot path.

#### Acceptance Criteria

1. THE F14_Map SHALL accept Hash, KeyEqual, and Allocator as template type parameters instead of storing function pointers in a runtime ops struct.
2. THE F14_Map SHALL invoke Hash as a direct call `hash_(key)` returning a `std::size_t`, enabling the compiler to inline the hash computation.
3. THE F14_Map SHALL invoke KeyEqual as a direct call `key_eq_(a, b)` returning `bool`, enabling the compiler to inline the equality check.
4. THE F14_Map SHALL invoke Allocator member functions `allocate(size_t, size_t)` and `deallocate(void*)` for chunk memory, enabling the compiler to inline allocation.
5. THE F14_Map SHALL not require a `mapped_to_key` function pointer; the Key type SHALL be directly accessible from the stored item type.
6. THE F14_Map SHALL derive the tag and chunk index from the hash value using `fmap_split_hash` logic: tag = `(hash >> 24) | 0x80`, chunk_index = `hash & chunk_mask`.

### Requirement 6: OVS Dependency Removal

**User Story:** As a developer, I want the F14 implementation free of Open vSwitch dependencies, so that the code compiles in the project's C++ Bazel build without requiring OVS headers.

#### Acceptance Criteria

1. THE F14_Map SHALL not include or depend on `util.h`, `ovs-atomic.h`, or `dynamic-string.h`.
2. THE F14_Map SHALL replace `OVS_LIKELY` and `OVS_UNLIKELY` branch hints with `[[likely]]` and `[[unlikely]]` C++20 attributes or compiler `__builtin_expect` intrinsics.
3. THE F14_Map SHALL replace `ovs_assert` with standard `assert` or a project-local assertion macro.
4. THE F14_Map SHALL replace `OVS_PREFETCH` with `__builtin_prefetch` or a portable prefetch wrapper.
5. THE F14_Map SHALL replace `xcalloc` and `xrealloc` with standard C++ allocation (operator new, std::vector, or the Allocator template parameter).
6. THE F14_Map SHALL replace OVS atomic operations (`atomic_read_explicit`, `atomic_store_explicit`) with `std::atomic` load/store operations.

### Requirement 7: ARM NEON SIMD Implementation

**User Story:** As a developer, I want the ARM NEON SIMD backend to use full 128-bit vector operations for tag matching, so that the F14 map achieves the same parallel comparison throughput on aarch64 as SSE2 provides on x86-64.

#### Acceptance Criteria

1. WHEN compiled on aarch64, THE SIMD_Backend SHALL load the 16-byte Chunk_Header using `vld1q_u8`.
2. WHEN compiled on aarch64, THE SIMD_Backend SHALL broadcast the needle tag byte using `vdupq_n_u8`.
3. WHEN compiled on aarch64, THE SIMD_Backend SHALL compare all 16 bytes simultaneously using `vceqq_u8`, producing a 128-bit mask of 0xFF (match) or 0x00 (no match) per byte.
4. WHEN compiled on aarch64, THE SIMD_Backend SHALL extract a per-byte bitmask from the NEON comparison result using a narrowing shift sequence (`vshrn_n_u16` + `vget_lane_u64`) or equivalent, producing a bitmask where bit N is set if byte N matched.
5. WHEN compiled on aarch64, THE SIMD_Backend SHALL mask the extracted bitmask to the low 14 bits (FMAP_FULL_MASK = 0x3FFF) to exclude the control and overflow bytes.
6. WHEN compiled on aarch64, THE SIMD_Backend SHALL use `__builtin_ctz` on the resulting bitmask to iterate matching positions, consistent with the SSE2 path.

### Requirement 8: Correctness and Testing Properties

**User Story:** As a developer, I want property-based and unit tests that verify the F14 map's correctness against a reference model, so that the port preserves the algorithm's invariants.

#### Acceptance Criteria

1. FOR ALL sequences of insert and erase operations, THE F14_Map size SHALL equal the number of distinct keys inserted minus the number of successfully erased keys (model-based invariant).
2. FOR ALL keys inserted into the F14_Map, a subsequent find with the same key SHALL return the corresponding value (round-trip property).
3. FOR ALL keys not inserted into the F14_Map, a find SHALL return the end iterator or not-found sentinel (negative lookup property).
4. FOR ALL keys erased from the F14_Map, a subsequent find with the same key SHALL return not-found (erase-find property).
5. FOR ALL valid F14_Map states, the SIMD_Backend TagMatch result SHALL equal the Scalar_Fallback TagMatch result for the same Chunk_Header and needle (cross-platform equivalence).
6. FOR ALL valid F14_Map states, the sum of occupied slots across all chunks SHALL equal the F14_Map size (structural invariant).
7. FOR ALL rehash operations, the F14_Map SHALL contain the same set of key-value pairs before and after rehash (rehash preservation).
8. WHEN the F14_Lookup_Table is used with the same sequence of Insert, Find, Remove, and EvictLru operations as the FastLookupTable, THE F14_Lookup_Table SHALL produce identical observable results (behavioral equivalence).
9. FOR ALL Chunk structures, the Overflow_Count and Hosted_Overflow_Count SHALL be consistent: the sum of Hosted_Overflow_Count across all chunks SHALL equal the total number of items stored in non-home chunks (overflow accounting invariant).


### Requirement 9: Item Iteration via Packed Begin

**User Story:** As a developer, I want the F14Map to support efficient iteration over all occupied items via a packed_begin pointer, so that I can traverse the map contents without scanning empty slots, matching the C fmap's `fmap_item_iter` semantics.

#### Acceptance Criteria

1. THE F14_Map SHALL accept a `bool EnableItemIteration` template parameter (defaulting to `true`) that controls whether packed_begin tracking and ItemIterator code are compiled in.
2. WHEN `EnableItemIteration` is true, THE F14_Map SHALL maintain a `packed_begin_` member of type `PackedPtr` that tracks the highest-address occupied item across all chunks.
3. THE F14_Map SHALL provide a `PackedPtr` struct matching the C fmap's `fmap_packed_ptr` encoding: the item pointer with the low 3 bits encoding the item index within the chunk.
4. THE F14_Map SHALL provide an `ItemIterator` type that traverses all occupied items by walking chunks backward from `packed_begin_`, matching the C fmap's `fmap_item_iter` semantics (`item_ptr`, `index`, advance through chunks).
5. THE ItemIterator SHALL provide three advance variants: `Advance()` (with EOF check and prefetch), `AdvancePrechecked()` (no EOF check, for internal use during erase), and `AdvanceLikelyDead()` (with EOF check, likely-dead hint for compiler dead-code elimination), matching the C fmap's `item_iter_advance`, `item_iter_advance_prechecked`, and `item_iter_advance_likely_dead`.
6. THE F14_Map SHALL provide `Begin()` and `End()` methods returning `ItemIterator`, where `Begin()` starts at `packed_begin_` and `End()` returns a sentinel with a null item pointer.
7. WHEN an item is inserted, THE F14_Map SHALL call `AdjPackedBeginAfterInsert` to update `packed_begin_` if the new item's packed pointer is greater than the current `packed_begin_`.
8. WHEN an item is erased, THE F14_Map SHALL call `AdjPackedBeginBeforeErase` to update `packed_begin_` if the erased item was the current begin, advancing to the next occupied item or resetting to zero if the map becomes empty.
9. WHEN `EnableItemIteration` is false, ALL packed_begin tracking, `AdjPackedBeginAfterInsert`, `AdjPackedBeginBeforeErase`, and ItemIterator code SHALL be compiled out via `if constexpr`, resulting in zero overhead.
10. FOR ALL valid F14_Map states with `EnableItemIteration=true`, iterating from `Begin()` to `End()` SHALL visit exactly `size()` items, each exactly once.
