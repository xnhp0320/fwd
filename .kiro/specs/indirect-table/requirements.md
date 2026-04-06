# Requirements Document

## Introduction

IndirectTable is a two-layer indirection system for mapping arbitrary keys to shared, reference-counted values in a DPDK data-plane environment. It provides many-to-one key-to-value mappings with automatic value deduplication, reference counting, and RCU-safe lockless reads for PMD threads. The system comprises two composable components: SlotArray (flat array of reference-counted value slots with deduplication via an intrusive reverse hash table) and IndirectTable (composes RcuHashTable, rte_mempool, and SlotArray into a complete key→value mapping).

## Glossary

- **SlotArray**: Reusable template component owning a flat array of ValueSlots, a free-ID stack, and an intrusive reverse hash table for O(1) value deduplication
- **IndirectTable**: Top-level component composing RcuHashTable (key lookup), rte_mempool (KeyEntry allocation), and SlotArray (value storage with dedup)
- **ValueSlot**: Cache-line aligned struct containing an IntrusiveRcuListHook, an atomic refcount, and a user-provided Value
- **KeyEntry**: Struct allocated from rte_mempool containing an IntrusiveRcuListHook, a Key, and a value_id index into the SlotArray
- **RcuHashTable**: Existing intrusive hash table with per-bucket locking and lockless reads, used for both key lookup and reverse value deduplication
- **Reverse_Map**: An RcuHashTable parameterized on ValueSlot, hashing and comparing on the embedded Value, used for O(1) value deduplication without duplicating the Value in memory
- **Free_Stack**: Array-based stack of available slot IDs providing O(1) allocation and deallocation
- **Refcount**: Atomic uint32_t in each ValueSlot tracking the number of KeyEntries referencing that slot; 0 means free, greater than 0 means in use
- **Control_Plane**: Single-writer thread responsible for all mutations (Insert, Remove, Update, Allocate, Deallocate)
- **PMD_Thread**: Poll-mode driver thread performing lockless reads via Get() and Find() without acquiring any locks
- **RCU_Grace_Period**: Time window after which all PMD threads are guaranteed to have completed any in-progress reads of a removed item
- **kInvalidId**: Sentinel value (UINT32_MAX) returned when allocation or lookup fails

## Requirements

### Requirement 1: SlotArray Initialization

**User Story:** As a developer, I want to initialize a SlotArray with a specified capacity and reverse map bucket count, so that the component is ready for allocation and deduplication operations.

#### Acceptance Criteria

1. WHEN Init is called with a valid Config, THE SlotArray SHALL allocate a contiguous array of ValueSlots using rte_zmalloc with cache-line alignment
2. WHEN Init is called with a valid Config, THE SlotArray SHALL initialize the Free_Stack with all slot IDs from 0 to capacity minus 1
3. WHEN Init is called with a valid Config, THE SlotArray SHALL initialize the Reverse_Map with the specified bucket count
4. WHEN Init completes successfully, THE SlotArray SHALL report used_count as 0 and capacity as the configured value

### Requirement 2: SlotArray Value Allocation with Deduplication

**User Story:** As a control-plane developer, I want to allocate value slots with automatic deduplication, so that identical values share a single slot and conserve memory.

#### Acceptance Criteria

1. WHEN FindOrAllocate is called with a value that exists in the Reverse_Map, THE SlotArray SHALL increment the Refcount of the existing slot and return the existing slot ID
2. WHEN FindOrAllocate is called with a value not in the Reverse_Map and free slots are available, THE SlotArray SHALL pop an ID from the Free_Stack, write the value, set Refcount to 1, insert the ValueSlot into the Reverse_Map, and return the new ID
3. WHEN FindOrAllocate is called and no free slots are available and the value is not in the Reverse_Map, THE SlotArray SHALL return kInvalidId without modifying any state
4. WHEN Allocate is called and free slots are available, THE SlotArray SHALL pop an ID from the Free_Stack, set Refcount to 1, and return the ID
5. WHEN Allocate is called and no free slots are available, THE SlotArray SHALL return kInvalidId without modifying any state

### Requirement 3: SlotArray Reference Counting

**User Story:** As a control-plane developer, I want to manage reference counts on value slots, so that shared slots are only freed when no keys reference them.

#### Acceptance Criteria

1. WHEN AddRef is called with a valid in-use slot ID, THE SlotArray SHALL increment the Refcount of that slot by 1
2. WHEN Release is called with a valid in-use slot ID, THE SlotArray SHALL decrement the Refcount by 1 and return true when the Refcount reaches 0
3. WHEN Release is called with a valid in-use slot ID, THE SlotArray SHALL decrement the Refcount by 1 and return false when the Refcount remains above 0

### Requirement 4: SlotArray Deallocation

**User Story:** As a control-plane developer, I want to deallocate value slots when their reference count reaches zero, so that slot IDs are recycled for future use.

#### Acceptance Criteria

1. WHEN Deallocate is called with a slot ID whose Refcount is 0, THE SlotArray SHALL remove the ValueSlot from the Reverse_Map and push the ID back onto the Free_Stack
2. THE SlotArray SHALL enforce that Deallocate is only called on slots with Refcount equal to 0

### Requirement 5: SlotArray Value Deduplication Invariant

**User Story:** As a system architect, I want the SlotArray to guarantee that no two in-use slots contain the same value, so that memory is not wasted on duplicate values.

#### Acceptance Criteria

1. THE SlotArray SHALL ensure that no two in-use slots contain equal values as determined by the ValueEqual functor
2. WHEN FindOrAllocate is called, THE SlotArray SHALL check the Reverse_Map before allocating a new slot
3. THE Reverse_Map SHALL reference ValueSlot nodes in-place via IntrusiveRcuListHook without duplicating the Value in memory

### Requirement 6: SlotArray Reverse Map Consistency

**User Story:** As a developer, I want the reverse map to faithfully mirror the in-use slot contents, so that value lookups and deduplication are always correct.

#### Acceptance Criteria

1. THE SlotArray SHALL maintain bidirectional consistency: every in-use slot is findable in the Reverse_Map, and every Reverse_Map entry points to a slot with Refcount greater than 0
2. WHEN FindByValue is called with a value present in an in-use slot, THE SlotArray SHALL return the corresponding slot ID
3. WHEN FindByValue is called with a value not present in any in-use slot, THE SlotArray SHALL return kInvalidId

### Requirement 7: SlotArray Free Stack Consistency

**User Story:** As a developer, I want the free stack to always account for all slot IDs, so that no IDs are lost or double-allocated.

#### Acceptance Criteria

1. THE SlotArray SHALL maintain the invariant that free_top plus used_count equals capacity at all times
2. THE SlotArray SHALL ensure that Allocate and FindOrAllocate never return the same ID twice without an intervening Deallocate

### Requirement 8: SlotArray Value Update

**User Story:** As a control-plane developer, I want to update a value in-place and have the reverse map rehash correctly, so that deduplication remains consistent after value changes.

#### Acceptance Criteria

1. WHEN UpdateValue is called with a valid in-use slot ID and a new value, THE SlotArray SHALL remove the old entry from the Reverse_Map, write the new value, and re-insert the slot into the Reverse_Map
2. WHEN UpdateValue completes, THE SlotArray SHALL ensure FindByValue with the new value returns the updated slot ID

### Requirement 9: SlotArray Lockless PMD Read

**User Story:** As a PMD thread developer, I want to read values by slot ID without acquiring any locks, so that the data-plane read path has minimal latency.

#### Acceptance Criteria

1. THE SlotArray SHALL provide a Get function that returns a pointer to the Value within the ValueSlot at the given ID via direct array indexing
2. THE SlotArray SHALL allocate the slots array using rte_zmalloc so that the base address is stable for the lifetime of the SlotArray, ensuring Get pointers remain valid

### Requirement 10: SlotArray Iteration

**User Story:** As a control-plane developer, I want to iterate over all in-use slots, so that I can implement diagnostics and CLI dump commands.

#### Acceptance Criteria

1. WHEN ForEachInUse is called, THE SlotArray SHALL invoke the callback for every slot with Refcount greater than 0, passing the slot ID and Value

### Requirement 11: IndirectTable Initialization

**User Story:** As a developer, I want to initialize an IndirectTable with key and value capacity parameters, so that the table is ready for key-value operations.

#### Acceptance Criteria

1. WHEN Init is called with a valid Config and RcuManager pointer, THE IndirectTable SHALL initialize the key RcuHashTable, create the rte_mempool for KeyEntry allocation, and initialize the SlotArray
2. WHEN Init completes successfully, THE IndirectTable SHALL be ready to accept Insert, Remove, and Find operations

### Requirement 12: IndirectTable Key-Value Insertion with Deduplication

**User Story:** As a control-plane developer, I want to insert key-value pairs where identical values are automatically deduplicated, so that many keys can share a single value slot.

#### Acceptance Criteria

1. WHEN Insert is called with a key not present in the key RcuHashTable, THE IndirectTable SHALL call FindOrAllocate on the SlotArray, allocate a KeyEntry from the rte_mempool, populate the KeyEntry with the key and value_id, insert the KeyEntry into the key RcuHashTable, and return the value_id
2. WHEN Insert is called with a key already present in the key RcuHashTable, THE IndirectTable SHALL return kInvalidId without modifying any state
3. WHEN Insert fails to allocate a KeyEntry from the rte_mempool, THE IndirectTable SHALL release the refcount acquired from FindOrAllocate, deallocate the slot if refcount reaches 0, and return kInvalidId
4. WHEN InsertWithId is called with a key not present and a valid value_id, THE IndirectTable SHALL call AddRef on the SlotArray, allocate a KeyEntry, and insert the KeyEntry into the key RcuHashTable
5. WHEN InsertWithId fails to allocate a KeyEntry from the rte_mempool, THE IndirectTable SHALL release the AddRef it performed and return false

### Requirement 13: IndirectTable Key Removal with RCU-Safe Reclamation

**User Story:** As a control-plane developer, I want to remove keys with deferred reclamation, so that PMD threads with in-flight reads are not disrupted.

#### Acceptance Criteria

1. WHEN Remove is called with a key present in the key RcuHashTable, THE IndirectTable SHALL unlink the KeyEntry from the hash table and schedule its retirement via RetireViaGracePeriod
2. WHEN the RCU_Grace_Period completes for a removed KeyEntry, THE IndirectTable SHALL return the KeyEntry to the rte_mempool and call Release on the SlotArray for the associated value_id
3. WHEN Release returns true (Refcount reached 0) after a grace period, THE IndirectTable SHALL call Deallocate on the SlotArray to recycle the slot
4. WHEN Remove is called with a key not present in the key RcuHashTable, THE IndirectTable SHALL return false without modifying any state

### Requirement 14: IndirectTable Refcount Invariant

**User Story:** As a system architect, I want the refcount of every value slot to exactly equal the number of KeyEntries referencing it, so that value slots are never leaked or prematurely freed.

#### Acceptance Criteria

1. THE IndirectTable SHALL maintain the invariant that for every in-use slot, the Refcount equals the number of KeyEntries whose value_id references that slot
2. THE IndirectTable SHALL increment the Refcount before inserting a KeyEntry into the key RcuHashTable
3. THE IndirectTable SHALL decrement the Refcount only after the RCU_Grace_Period completes for a removed KeyEntry

### Requirement 15: IndirectTable Lockless PMD Read Path

**User Story:** As a PMD thread developer, I want to look up values by key without acquiring any locks, so that packet processing is not blocked by control-plane mutations.

#### Acceptance Criteria

1. THE IndirectTable SHALL provide a Find function that performs a lockless lookup in the key RcuHashTable and returns a pointer to the KeyEntry
2. THE IndirectTable SHALL provide Prefetch and FindWithPrefetch functions to enable batched cache-line prefetching across a packet batch before resolving lookups
3. THE IndirectTable SHALL expose the SlotArray via slot_array() so that PMD threads can call Get(value_id) to obtain a pointer to the Value

### Requirement 16: IndirectTable Value Update

**User Story:** As a control-plane developer, I want to update a value in-place by slot ID, so that all keys sharing that value see the new value without re-insertion.

#### Acceptance Criteria

1. WHEN UpdateValue is called with a valid value_id and a new value, THE IndirectTable SHALL delegate to SlotArray UpdateValue to update the value and rehash in the Reverse_Map

### Requirement 17: IndirectTable Key Iteration

**User Story:** As a control-plane developer, I want to iterate over all key entries, so that I can implement diagnostics and CLI dump commands.

#### Acceptance Criteria

1. WHEN ForEachKey is called, THE IndirectTable SHALL invoke the callback for every KeyEntry in the key RcuHashTable, passing the key and value_id

### Requirement 18: Error Handling

**User Story:** As a developer, I want all error conditions to be handled gracefully with full rollback, so that the system remains consistent after any failure.

#### Acceptance Criteria

1. IF the SlotArray is full during FindOrAllocate, THEN THE SlotArray SHALL return kInvalidId without modifying any state
2. IF the rte_mempool is exhausted during Insert, THEN THE IndirectTable SHALL rollback the refcount acquired from FindOrAllocate and return kInvalidId
3. IF the rte_mempool is exhausted during InsertWithId, THEN THE IndirectTable SHALL rollback the AddRef and return false
4. IF Remove is called with a non-existent key, THEN THE IndirectTable SHALL return false without modifying any state
5. IF an invalid slot ID (greater than or equal to capacity) is passed to Get, AddRef, Release, or Deallocate, THEN THE SlotArray SHALL trigger an assertion failure in debug builds
