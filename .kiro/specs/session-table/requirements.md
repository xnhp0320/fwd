# Requirements Document

## Introduction

This feature adds a shared, concurrent SessionTable to the DPDK-based packet processing application. The SessionTable provides an authoritative session index backed by DPDK's `rte_hash` (lock-free readers, per-bucket spinlock writers) and `rte_mempool` (MPMC session entry allocation). It operates as a shared L2 layer beneath the existing per-PMD FastLookupTable (L1 cache), enabling session-aware packet processing across all PMD threads.

The two-tier architecture works as follows: the FastLookupTable remains a per-thread, single-threaded stateless flow cache. Each LookupEntry in the FastLookupTable gains a pointer to a SessionEntry and a cached version number. On L1 hit, the PMD thread validates the cached version against the SessionEntry's authoritative version via a relaxed atomic load. A version match confirms the session is still valid (fast path). A mismatch triggers invalidation and fallback to the SessionTable (L2 lookup). On L1 miss, the PMD thread queries the SessionTable directly and caches the result.

SessionEntry memory is owned by an `rte_mempool` (SessionPool). Entries are never freed in the traditional sense — they are recycled with a version bump. The control plane is the sole writer for version bumps and deletions. PMD threads insert new sessions and update timestamps. RCU QSBR integration via `rte_hash_rcu_qsbr_add()` provides automatic deferred slot reclamation using the existing RcuManager's QSBR variable.

### Out of Scope

- Control plane timeout scanning logic (the ForEach API is provided but timeout processing is deferred)
- TCP state tracking
- Modifications to the FastLookupTable class implementation (`fast_lookup_table.h`): the `Insert`/`Find`/`Remove`/`ForEach` methods and the `absl::flat_hash_set` + `ListSlab` internals remain unchanged. The `LookupEntry` struct IS modified (new `session` pointer and `cached_version` fields), and the two-tier lookup logic lives in `FiveTupleForwardingProcessor`.

## Glossary

- **Session_Table**: The shared, concurrent session index backed by `rte_hash`. Provides insert, lookup, delete, and forEach operations. Keyed by 5-tuple + zone_id.
- **Session_Pool**: An `rte_mempool`-based allocator that owns all SessionEntry memory. Supports MPMC get/put for cross-thread allocation and recycling.
- **Session_Entry**: A cache-line-aligned structure containing session state: an atomic version counter and an atomic last-seen timestamp. Allocated from the Session_Pool.
- **Session_Key**: The lookup key for the Session_Table: source IP, destination IP, source port, destination port, protocol, and zone_id. Does NOT include VNI.
- **Version**: A `std::atomic<uint32_t>` field on Session_Entry, bumped by the Control_Plane on recycle to lazily invalidate all cached references without per-PMD notification.
- **Cached_Version**: A non-atomic `uint32_t` stored in LookupEntry alongside the Session_Entry pointer. Compared against the authoritative version for fast-path validation.
- **Zone_ID**: A `uint32_t` identifier that partitions sessions into logical zones. Part of the Session_Key. Distinct from VNI.
- **Fast_Lookup_Table**: The existing per-PMD, single-threaded flow cache (`absl::flat_hash_set` + `ListSlab`). Serves as the L1 cache. NOT modified by this feature.
- **Lookup_Entry**: The existing 64-byte cache-aligned entry in Fast_Lookup_Table. Extended with a Session_Entry pointer and Cached_Version field.
- **Control_Plane**: The boost.asio event loop on the main lcore. Sole writer for session deletion and version bumps.
- **PMD_Thread**: A DPDK poll-mode driver worker thread. Inserts new sessions and updates timestamps. Reads sessions concurrently.
- **RCU_Manager**: The existing async RCU system that manages the QSBR variable, thread registration, and grace period callbacks.
- **Processor_Context**: The per-thread context struct passed to processor launchers. Extended with a `session_table` pointer.

## Requirements

### Requirement 1: SessionEntry Structure

**User Story:** As a packet processor developer, I want a cache-line-aligned session entry structure, so that concurrent reads and writes from multiple PMD threads and the control plane operate on well-defined atomic fields without false sharing.

#### Acceptance Criteria

1. THE Session_Entry SHALL be aligned to 64 bytes (one cache line) and contain a `version` field of type `std::atomic<uint32_t>` and a `timestamp` field of type `std::atomic<uint64_t>`.
2. THE Session_Entry `version` field SHALL be initialized to 1 when the Session_Entry is first allocated from the Session_Pool for a new session.
3. WHEN a PMD_Thread updates the `timestamp` field, THE PMD_Thread SHALL use `std::memory_order_relaxed` for the atomic store.
4. WHEN a PMD_Thread or the Control_Plane reads the `version` field for validation, THE reader SHALL use `std::memory_order_relaxed` for the atomic load.

### Requirement 2: SessionPool (rte_mempool)

**User Story:** As a system architect, I want session entry memory managed by an `rte_mempool`, so that cross-thread allocation and recycling of session entries is safe without the single-thread ownership constraint of ListSlab.

#### Acceptance Criteria

1. WHEN the Session_Pool is created, THE Session_Pool SHALL allocate an `rte_mempool` sized to hold the configured session capacity of Session_Entry objects.
2. THE Session_Pool SHALL use `rte_mempool_create()` with MPMC (multi-producer multi-consumer) cache settings so that any PMD_Thread can allocate and the Control_Plane can recycle entries.
3. WHEN a Session_Entry is obtained from the Session_Pool via `rte_mempool_get()`, THE Session_Pool SHALL NOT clear the object contents, so that the `version` field survives recycling.
4. IF `rte_mempool_get()` fails because the pool is exhausted, THEN THE Session_Pool SHALL return a null pointer to the caller.
5. WHEN the Session_Pool is destroyed, THE Session_Pool SHALL free the underlying `rte_mempool`.

### Requirement 3: SessionTable (rte_hash)

**User Story:** As a system architect, I want a shared concurrent hash table backed by `rte_hash`, so that all PMD threads can look up and insert sessions while the control plane can delete sessions, with lock-free read concurrency and fine-grained per-bucket write locking.

#### Acceptance Criteria

1. WHEN the Session_Table is created, THE Session_Table SHALL create an `rte_hash` with flags `RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF` and `RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD` to enable lock-free readers and per-bucket spinlock multi-writer support.
2. THE Session_Table SHALL use Session_Key (source IP, destination IP, source port, destination port, protocol, zone_id) as the hash key. THE Session_Key SHALL NOT include VNI.
3. WHEN the Session_Table is created, THE Session_Table SHALL attach the RCU_Manager's QSBR variable via `rte_hash_rcu_qsbr_add()` for automatic deferred slot reclamation.
4. WHEN the Session_Table is destroyed, THE Session_Table SHALL free the underlying `rte_hash`.
5. IF `rte_hash_create()` fails, THEN THE Session_Table creation SHALL return an error status indicating the failure reason.

### Requirement 4: Session Insert Operation

**User Story:** As a PMD thread developer, I want to insert new sessions into the shared SessionTable, so that newly observed flows are tracked and visible to all PMD threads.

#### Acceptance Criteria

1. WHEN a PMD_Thread calls insert with a Session_Key, THE Session_Table SHALL allocate a Session_Entry from the Session_Pool, initialize the `version` to 1 and the `timestamp` to the current value, and insert the key into the `rte_hash` with the Session_Entry pointer as associated data.
2. IF the Session_Key already exists in the `rte_hash`, THEN THE Session_Table SHALL return the existing Session_Entry pointer without allocating a new entry.
3. IF the Session_Pool is exhausted, THEN THE Session_Table insert SHALL return a null pointer.
4. IF `rte_hash_add_key_data()` fails, THEN THE Session_Table SHALL return the Session_Entry to the Session_Pool and return a null pointer.

### Requirement 5: Session Lookup Operation

**User Story:** As a PMD thread developer, I want to look up sessions by 5-tuple and zone_id, so that I can retrieve session state for packets belonging to existing flows.

#### Acceptance Criteria

1. WHEN a PMD_Thread calls lookup with a Session_Key, THE Session_Table SHALL call `rte_hash_lookup_data()` and return the associated Session_Entry pointer if found.
2. IF the Session_Key is not found in the `rte_hash`, THEN THE Session_Table lookup SHALL return a null pointer.

### Requirement 6: Session Delete Operation

**User Story:** As a control plane developer, I want to delete sessions from the SessionTable, so that expired or invalid sessions are removed and their entries recycled for reuse.

#### Acceptance Criteria

1. WHEN the Control_Plane calls delete with a Session_Key, THE Session_Table SHALL bump the `version` field on the associated Session_Entry using a relaxed atomic load, increment, and relaxed atomic store sequence (single-writer pattern, NOT `fetch_add`).
2. WHEN the Control_Plane calls delete, THE Session_Table SHALL return the Session_Entry to the Session_Pool after bumping the version.
3. WHEN the Control_Plane calls delete, THE Session_Table SHALL remove the key from the `rte_hash` via `rte_hash_del_key()`.
4. IF the Session_Key is not found in the `rte_hash`, THEN THE Session_Table delete SHALL return an error status indicating the key was not found.

### Requirement 7: Session ForEach with Deletion Support

**User Story:** As a control plane developer, I want to iterate over all sessions with the ability to delete entries during iteration, so that future timeout scanning can efficiently walk the table and remove expired sessions.

#### Acceptance Criteria

1. WHEN the Control_Plane calls forEach with a callback, THE Session_Table SHALL iterate over all occupied entries in the `rte_hash` using `rte_hash_iterate()`.
2. THE forEach callback SHALL receive the Session_Key and Session_Entry pointer for each entry and return a boolean indicating whether the entry should be deleted.
3. WHEN the forEach callback returns true (requesting deletion), THE Session_Table SHALL perform the same delete sequence as Requirement 6 (version bump, return to pool, remove from hash).
4. THE forEach operation SHALL be callable only from the Control_Plane thread.

### Requirement 8: LookupEntry Session Pointer Extension

**User Story:** As a packet processor developer, I want each LookupEntry in the FastLookupTable to carry a pointer to its corresponding SessionEntry and a cached version, so that the fast path can validate session liveness without accessing the shared SessionTable.

#### Acceptance Criteria

1. THE Lookup_Entry SHALL contain a `Session_Entry*` field (named `session`) initialized to `nullptr` and a `uint32_t` field (named `cached_version`) initialized to 0.
2. THE Lookup_Entry SHALL remain exactly 64 bytes (one cache line) after adding the new fields, by reclaiming existing padding bytes.
3. WHEN a Lookup_Entry is populated from a Session_Table lookup result, THE caller SHALL store the Session_Entry pointer and the current version value in the Lookup_Entry's `session` and `cached_version` fields.

### Requirement 9: Version-Based Lazy Invalidation

**User Story:** As a packet processor developer, I want a version-based validation mechanism on the fast path, so that stale session references are detected and refreshed without requiring explicit invalidation messages from the control plane.

#### Acceptance Criteria

1. WHEN a PMD_Thread finds a Lookup_Entry in the Fast_Lookup_Table with a non-null `session` pointer, THE PMD_Thread SHALL compare `cached_version` with `session->version.load(std::memory_order_relaxed)`.
2. WHEN the `cached_version` matches the Session_Entry's current version, THE PMD_Thread SHALL treat the session as valid and update the Session_Entry's `timestamp` field.
3. WHEN the `cached_version` does not match the Session_Entry's current version, THE PMD_Thread SHALL set the Lookup_Entry's `session` pointer to `nullptr` and `cached_version` to 0, then fall through to a Session_Table lookup.
4. WHEN a PMD_Thread finds a Lookup_Entry with a null `session` pointer (either initial state or after invalidation), THE PMD_Thread SHALL perform a Session_Table lookup and, if found, cache the Session_Entry pointer and current version in the Lookup_Entry.

### Requirement 10: FiveTupleForwardingProcessor Integration

**User Story:** As a packet processor developer, I want the FiveTupleForwardingProcessor to integrate with the SessionTable for two-tier lookup, so that session state is tracked across all PMD threads while maintaining fast-path performance through the per-thread L1 cache.

#### Acceptance Criteria

1. WHEN the FiveTupleForwardingProcessor processes a packet and the Fast_Lookup_Table returns a hit, THE FiveTupleForwardingProcessor SHALL perform version-based validation as specified in Requirement 9.
2. WHEN the Fast_Lookup_Table returns a miss, THE FiveTupleForwardingProcessor SHALL look up the Session_Table using the packet's 5-tuple with zone_id set to 0.
3. WHEN the Session_Table lookup returns a miss, THE FiveTupleForwardingProcessor SHALL insert a new session into the Session_Table with zone_id set to 0.
4. WHEN the Session_Table lookup or insert returns a valid Session_Entry, THE FiveTupleForwardingProcessor SHALL cache the Session_Entry pointer and version in the Lookup_Entry.
5. WHILE the `session_table` pointer in Processor_Context is null, THE FiveTupleForwardingProcessor SHALL skip all Session_Table operations and operate using only the Fast_Lookup_Table (backward compatible).

### Requirement 11: ProcessorContext Extension

**User Story:** As a system integrator, I want the ProcessorContext to carry a pointer to the SessionTable, so that any processor can access the shared session state without additional coupling.

#### Acceptance Criteria

1. THE Processor_Context SHALL contain a `session_table` field of type `void*` initialized to `nullptr`.
2. WHEN the Control_Plane initializes the Session_Table, THE Control_Plane SHALL store the Session_Table pointer in the Processor_Context passed to each PMD_Thread.
3. WHILE the `session_table` field is `nullptr`, THE Processor_Context SHALL indicate that session tracking is disabled.

### Requirement 12: SessionTable Ownership and Lifecycle

**User Story:** As a system architect, I want the ControlPlane to own the SessionTable and SessionPool, so that their lifecycle is tied to the application lifecycle and they are properly cleaned up on shutdown.

#### Acceptance Criteria

1. WHEN the Control_Plane is initialized with a session capacity greater than 0, THE Control_Plane SHALL create the Session_Pool and Session_Table, attach the RCU_Manager's QSBR variable, and store the Session_Table pointer for distribution to PMD_Threads via Processor_Context.
2. WHEN the session capacity is 0, THE Control_Plane SHALL NOT create a Session_Pool or Session_Table, and the `session_table` pointer in Processor_Context SHALL remain `nullptr`.
3. WHEN the Control_Plane is destroyed, THE Control_Plane SHALL destroy the Session_Table before destroying the Session_Pool, ensuring all hash entries are cleaned up before the backing memory pool is freed.
4. THE session capacity SHALL be configurable via a `session_capacity` field in the application configuration.

### Requirement 13: SessionTable Configuration

**User Story:** As an operator, I want to configure the session table capacity, so that I can size the session tracking system appropriately for the expected number of concurrent flows.

#### Acceptance Criteria

1. THE application configuration SHALL support a `session_capacity` field specifying the maximum number of concurrent sessions.
2. WHEN `session_capacity` is set to 0, THE application SHALL disable session tracking entirely (no Session_Pool or Session_Table created).
3. IF `session_capacity` is set to a value that is not a positive integer or zero, THEN THE configuration validator SHALL return an error.
