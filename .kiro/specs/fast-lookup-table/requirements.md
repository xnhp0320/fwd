# Requirements Document

## Introduction

This feature introduces two components in the `rxtx` module: a `ListSlab` memory pool and a `FastLookupTable` hash table. The `ListSlab` is a fixed-capacity, slab-style allocator that pre-allocates a contiguous block of items and manages free/used items via a boost intrusive linked list. The `FastLookupTable` is a flow lookup table backed by `absl::flat_hash_map` that maps 5-tuple + VNI keys (reusing the existing `IpAddress` union) to slab-allocated entries, with pointer-based hashing and equality that dereferences into slab memory. Both components reside in the `rxtx` directory and namespace, are single-threaded (no locks), and target cache-line-aligned entry layout for DPDK data-plane performance.

## Glossary

- **ListSlab**: A templated class in the `rxtx` namespace that pre-allocates a fixed number of items of type T and manages them via a boost intrusive singly- or doubly-linked free list.
- **Slab_Entry**: An individual item managed by the ListSlab. Each Slab_Entry contains a boost intrusive list hook (the intrusive node overhead) plus the user-defined payload fields.
- **Free_List**: A boost intrusive list head within the ListSlab that chains all currently unallocated Slab_Entry items.
- **Intrusive_Hook**: The boost::intrusive::list_member_hook (or slist_member_hook) embedded in each Slab_Entry, enabling O(1) insertion and removal from the Free_List without external node allocation.
- **Allocator**: A policy type or template parameter on ListSlab that controls how the underlying contiguous memory block is allocated and freed (e.g., `operator new` / `operator delete` for standard allocation, or `rte_malloc` / `rte_free` for DPDK huge-page allocation).
- **FastLookupTable**: A class in the `rxtx` namespace that wraps an `absl::flat_hash_map` to provide flow-level lookup by 5-tuple + VNI key.
- **Lookup_Entry**: A cache-line-aligned structure allocated by the ListSlab that stores the lookup key fields: source IP, destination IP, source port, destination port, protocol, VNI, and a flags field indicating IPv4 or IPv6.
- **Lookup_Key_Pointer**: A pointer to a Lookup_Entry. The `absl::flat_hash_map` stores these pointers as both key and value; hashing and equality are performed by dereferencing the pointer into the Lookup_Entry fields.
- **IpAddress**: The existing `rxtx::IpAddress` union that stores either a 32-bit IPv4 address or a 128-bit IPv6 address, reused from `rxtx/packet_metadata.h`.
- **Five_Tuple**: The combination of source IP address, destination IP address, source L4 port, destination L4 port, and IP protocol number.
- **VNI**: VXLAN Network Identifier, a 32-bit unsigned integer identifying a VXLAN segment.
- **Cache_Line**: 64 bytes, the hardware cache line size defined as `kCacheLineSize` in `rxtx/packet.h`.

## Requirements

### Requirement 1: ListSlab Memory Pre-allocation

**User Story:** As a data-plane developer, I want a slab allocator that pre-allocates a fixed number of items at construction time, so that allocation and deallocation during packet processing are O(1) with no system calls.

#### Acceptance Criteria

1. WHEN a ListSlab of capacity N is constructed, THE ListSlab SHALL allocate a single contiguous memory block large enough to hold N Slab_Entry items using the configured Allocator.
2. WHEN a ListSlab of capacity N is constructed, THE ListSlab SHALL insert all N Slab_Entry items into the Free_List.
3. THE ListSlab SHALL maintain a free_count field that equals the number of items currently in the Free_List.
4. THE ListSlab SHALL maintain a used_count field that equals N minus free_count.
5. WHEN the ListSlab is destroyed, THE ListSlab SHALL deallocate the contiguous memory block using the configured Allocator.
6. THE ListSlab SHALL accept the capacity N as a constructor parameter.

### Requirement 2: ListSlab Allocate and Deallocate Operations

**User Story:** As a data-plane developer, I want O(1) allocate and deallocate operations on the slab, so that I can acquire and release entries during packet processing without performance degradation.

#### Acceptance Criteria

1. WHEN Allocate is called and the Free_List is non-empty, THE ListSlab SHALL remove one Slab_Entry from the Free_List, increment used_count, decrement free_count, and return a pointer to the Slab_Entry.
2. WHEN Allocate is called and the Free_List is empty, THE ListSlab SHALL return a null pointer.
3. WHEN Deallocate is called with a valid Slab_Entry pointer, THE ListSlab SHALL insert the Slab_Entry back into the Free_List, decrement used_count, and increment free_count.
4. THE Allocate operation SHALL execute in O(1) time.
5. THE Deallocate operation SHALL execute in O(1) time.

### Requirement 3: ListSlab Intrusive List Integration

**User Story:** As a data-plane developer, I want the slab to use boost intrusive lists for the free list, so that no additional heap allocation is needed for list bookkeeping.

#### Acceptance Criteria

1. THE ListSlab SHALL use a boost::intrusive list (list or slist) as the Free_List implementation.
2. THE Slab_Entry type T SHALL contain an Intrusive_Hook member that satisfies the boost intrusive list requirements.
3. THE ListSlab SHALL provide a compile-time check (using static_assert with type traits or SFINAE) that verifies type T contains a valid Intrusive_Hook member, since C++20 concepts are not available in C++17.
4. THE Intrusive_Hook overhead SHALL be accounted for in the Slab_Entry memory layout and in the total memory block size calculation.

### Requirement 4: ListSlab Allocator Customization

**User Story:** As a data-plane developer, I want to customize the memory allocator used by the slab, so that I can use standard heap allocation in tests and DPDK huge-page allocation (`rte_malloc` / `rte_free`) in production.

#### Acceptance Criteria

1. THE ListSlab SHALL accept an Allocator as a template parameter that controls how the contiguous memory block is allocated and freed.
2. WHEN the Allocator is the default (standard) allocator, THE ListSlab SHALL use `operator new` and `operator delete` (or equivalent aligned allocation) for the memory block.
3. WHEN the Allocator is a DPDK allocator, THE ListSlab SHALL use `rte_malloc` and `rte_free` for the memory block.
4. THE Allocator interface SHALL provide at minimum an `allocate(size_t bytes, size_t alignment)` function and a `deallocate(void* ptr)` function.

### Requirement 5: ListSlab Thread Safety

**User Story:** As a data-plane developer, I want the slab to be explicitly single-threaded, so that there is no lock overhead on the fast path.

#### Acceptance Criteria

1. THE ListSlab SHALL contain no mutexes, spinlocks, or atomic operations.
2. THE ListSlab SHALL document in its class comment that it is not thread-safe and must be accessed from a single thread.

### Requirement 6: Lookup_Entry Layout

**User Story:** As a data-plane developer, I want the lookup entry to be cache-line aligned and contain the 5-tuple + VNI + flags, so that hash table lookups access a single cache line for the key comparison.

#### Acceptance Criteria

1. THE Lookup_Entry SHALL contain the following fields: source IpAddress, destination IpAddress, source port (uint16_t), destination port (uint16_t), protocol (uint8_t), VNI (uint32_t), and a flags field indicating IPv4 or IPv6.
2. THE Lookup_Entry SHALL reuse the existing `rxtx::IpAddress` union for IP address storage.
3. THE Lookup_Entry SHALL include an Intrusive_Hook member for ListSlab management.
4. THE Lookup_Entry total size (including the Intrusive_Hook) SHALL be aligned to a Cache_Line boundary (64 bytes), verified by a static_assert.
5. THE Lookup_Entry key fields (excluding the Intrusive_Hook) SHALL be laid out so that a single cache line fetch retrieves all fields needed for hashing and equality comparison.

### Requirement 7: FastLookupTable Hash Map

**User Story:** As a data-plane developer, I want a hash table that maps 5-tuple + VNI keys to slab-allocated entries, so that I can perform fast flow lookups during packet processing.

#### Acceptance Criteria

1. THE FastLookupTable SHALL use `absl::flat_hash_map` as the underlying hash map implementation.
2. THE FastLookupTable SHALL store Lookup_Key_Pointer values in the hash map, where each pointer references a Lookup_Entry allocated by the ListSlab.
3. THE FastLookupTable SHALL provide a custom hash functor that dereferences the Lookup_Key_Pointer to compute a hash over the key fields of the Lookup_Entry.
4. THE FastLookupTable SHALL provide a custom equality functor that dereferences two Lookup_Key_Pointer values and compares the key fields of the referenced Lookup_Entry items.
5. THE FastLookupTable SHALL own a ListSlab instance for allocating and deallocating Lookup_Entry items.

### Requirement 8: IPv4-Optimized Hashing and Equality

**User Story:** As a data-plane developer, I want the hash and equality functions to use only 4 bytes for IPv4 addresses instead of 16, so that IPv4 lookups are faster than hashing the full 16-byte union.

#### Acceptance Criteria

1. WHEN the Lookup_Entry flags field indicates IPv4, THE hash functor SHALL hash only the 4-byte `IpAddress.v4` member for both source and destination addresses.
2. WHEN the Lookup_Entry flags field indicates IPv6, THE hash functor SHALL hash all 16 bytes of `IpAddress.v6` for both source and destination addresses.
3. WHEN the Lookup_Entry flags field indicates IPv4, THE equality functor SHALL compare only the 4-byte `IpAddress.v4` member for both source and destination addresses.
4. WHEN the Lookup_Entry flags field indicates IPv6, THE equality functor SHALL compare all 16 bytes of `IpAddress.v6` for both source and destination addresses.
5. WHEN two Lookup_Entry items have different flags values (one IPv4, one IPv6), THE equality functor SHALL return false.

### Requirement 9: FastLookupTable Insert, Lookup, and Remove

**User Story:** As a data-plane developer, I want insert, lookup, and remove operations on the table, so that I can manage flow entries during packet processing.

#### Acceptance Criteria

1. WHEN Insert is called with 5-tuple + VNI + flags, THE FastLookupTable SHALL allocate a Lookup_Entry from the ListSlab, populate the key fields, and insert the Lookup_Key_Pointer into the hash map.
2. WHEN Insert is called and the ListSlab is full, THE FastLookupTable SHALL return an error or null pointer indicating allocation failure.
3. WHEN Insert is called with a key that already exists in the hash map, THE FastLookupTable SHALL return the existing entry without allocating a new one.
4. WHEN Lookup is called with 5-tuple + VNI + flags, THE FastLookupTable SHALL return a pointer to the matching Lookup_Entry if found, or a null pointer if not found.
5. WHEN Remove is called with a Lookup_Key_Pointer, THE FastLookupTable SHALL erase the entry from the hash map and deallocate the Lookup_Entry back to the ListSlab.
6. WHEN Remove is called with a key that does not exist in the hash map, THE FastLookupTable SHALL perform no action.

### Requirement 10: FastLookupTable Thread Safety

**User Story:** As a data-plane developer, I want the lookup table to be explicitly single-threaded, so that there is no lock overhead on the fast path.

#### Acceptance Criteria

1. THE FastLookupTable SHALL contain no mutexes, spinlocks, or atomic operations.
2. THE FastLookupTable SHALL document in its class comment that it is not thread-safe and must be accessed from a single thread.

### Requirement 11: Modification Toggle

**User Story:** As a data-plane developer, I want to disable modifications to the lookup table at runtime, so that I can protect the table from unintended inserts or removes during read-only phases of packet processing.

#### Acceptance Criteria

1. THE FastLookupTable SHALL maintain a modifiable flag that controls whether Insert and Remove operations are permitted.
2. WHEN the FastLookupTable is constructed, THE FastLookupTable SHALL set the modifiable flag to true.
3. WHEN SetModifiable is called with a boolean argument, THE FastLookupTable SHALL update the modifiable flag to the provided value.
4. WHEN IsModifiable is called, THE FastLookupTable SHALL return the current value of the modifiable flag.
5. WHEN Insert is called and the modifiable flag is false, THE FastLookupTable SHALL return a null pointer without allocating a Lookup_Entry or modifying the hash map.
6. WHEN Remove is called and the modifiable flag is false, THE FastLookupTable SHALL return false without erasing any entry from the hash map or deallocating any Lookup_Entry.
7. THE modifiable flag SHALL be implemented as a `std::atomic<bool>` to allow safe cross-thread access (SetModifiable called from the ControlPlane thread, Insert/Remove/ForEach reading from the PMD thread).
8. THE SetModifiable store SHALL use `std::memory_order_release` so that the flag write is visible to the PMD thread before any deferred callback (enqueued via `call_after_grace_period`) executes.
9. THE modifiable flag reads in Insert, Remove, and IsModifiable SHALL use `std::memory_order_acquire` to pair with the release store in SetModifiable.

### Requirement 12: User-Controlled Iteration API

**User Story:** As a data-plane developer, I want to iterate over entries in the lookup table in caller-controlled batches, so that I can spread iteration work across multiple processing cycles without blocking the data plane.

#### Acceptance Criteria

1. THE FastLookupTable SHALL expose an Iterator type that wraps the underlying `absl::flat_hash_map` iterator.
2. THE Iterator SHALL be stored and managed by the caller, not by the FastLookupTable.
3. WHEN Begin is called, THE FastLookupTable SHALL return an Iterator pointing to the first entry in the hash map.
4. WHEN End is called, THE FastLookupTable SHALL return a past-the-end Iterator suitable for comparison.
5. WHEN ForEach is called with an Iterator reference, a count, and a callable fn, THE FastLookupTable SHALL apply fn to up to count entries starting from the current Iterator position, advancing the Iterator past each visited entry.
6. THE callable fn SHALL receive a LookupEntry pointer and return a bool: true to request removal of the visited entry, false to keep it.
7. WHEN fn returns true for an entry, THE FastLookupTable SHALL erase that entry from the hash set and deallocate it back to the ListSlab, advancing the Iterator safely using the return value of the erase operation.
8. WHEN the Iterator reaches the end during a ForEach call, THE FastLookupTable SHALL stop early and return the number of entries actually visited.
9. THE ForEach method SHALL NOT be const-qualified, since fn may trigger entry removal.
10. WHEN Insert or Remove is performed on the FastLookupTable outside of ForEach, THE Iterator SHALL be considered invalidated, following standard `absl::flat_hash_set` iterator invalidation semantics.
11. THE FastLookupTable SHALL document that the caller is responsible for not using an invalidated Iterator after standalone Insert or Remove calls.
