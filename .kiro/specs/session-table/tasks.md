# Implementation Tasks

## Task 1: SessionEntry and SessionKey structs
- [x] Create `session/session_entry.h` with `SessionEntry` struct (alignas(64), atomic version + timestamp, static_asserts)
- [x] Create `session/session_key.h` with `SessionKey` struct (44 bytes, `FromMetadata` helper, explicit padding, static_assert)
- [x] Create `session/BUILD` with `session_entry` and `session_key` cc_library targets
- [x] Verify both structs compile and static_asserts pass

## Task 2: SessionTable core implementation
- [x] Create `session/session_table.h` with `SessionTable` class declaration (Init, Lookup, Insert, Delete, ForEach, BumpVersion, ReturnToPool)
- [x] Create `session/session_table.cc` with `Init()` — create rte_mempool (MPMC, no obj_init), create rte_hash (RW_CONCURRENCY_LF + MULTI_WRITER_ADD), attach QSBR via rte_hash_rcu_qsbr_add (DQ mode)
- [x] Implement `Lookup()` — rte_hash_lookup_data, return SessionEntry* or nullptr
- [x] Implement `Insert()` — check existing via Lookup, rte_mempool_get, init version=1 + timestamp, rte_hash_add_key_data, return to pool on failure
- [x] Implement `Delete()` — lookup, BumpVersion (relaxed load+store), rte_hash_del_key, rte_mempool_put
- [x] Implement `ForEach()` — rte_hash_iterate, callback returns bool for deletion, same delete sequence
- [x] Implement destructor — ForEach to return all entries to pool, rte_hash_free, rte_mempool_free
- [x] Add `session_table` cc_library target to `session/BUILD`

## Task 3: SessionTable unit tests
- [x] Create `session/session_table_test.cc` with Google Test (no property-based tests)
- [x] Test SessionKey::FromMetadata populates all fields correctly, padding zeroed
- [x] Test Init with valid capacity succeeds, Init with capacity=0 returns error, Init with nullptr qsbr_var succeeds
- [x] Test Insert returns non-null with version=1, Insert duplicate returns same pointer
- [x] Test Lookup finds existing key, returns nullptr for missing key
- [x] Test Delete bumps version, returns NotFoundError for missing key, Lookup returns nullptr after delete
- [x] Test ForEach visits all entries, ForEach with deletion removes entries, ForEach on empty returns 0
- [x] Test pool exhaustion: insert until pool full, next insert returns nullptr
- [x] Add `session_table_test` cc_test target to `session/BUILD`

## Task 4: LookupEntry extension
- [x] Add `uint32_t cached_version = 0` and `void* session = nullptr` fields to `LookupEntry` in `rxtx/lookup_entry.h`, replacing 12 bytes of trailing padding
- [x] Update memory layout comment to reflect new fields at offsets 52 and 56
- [x] Verify `static_assert(sizeof(LookupEntry) == 64)` still passes
- [x] Verify `LookupEntryHash` and `LookupEntryEq` do NOT include `session` or `cached_version`
- [x] Update `rxtx/fast_lookup_table_test.cc` to verify new fields default to 0/nullptr and don't affect hash/eq

## Task 5: ProcessorContext extension
- [x] Add `void* session_table = nullptr` field to `ProcessorContext` in `processor/processor_context.h`

## Task 6: Configuration extension
- [x] Add `uint32_t session_capacity = 0` field to `DpdkConfig` in `config/dpdk_config.h`
- [x] Update `config/config_parser.cc` to parse `"session_capacity"` from top-level JSON
- [x] Update `config/config_validator.cc` to validate `session_capacity` (uint32_t, always valid)
- [x] Update `config/config_printer.cc` to print `session_capacity`
- [x] Update `config/config_parser_test.cc` to test parsing `session_capacity`
- [x] Update `config/config_validator_test.cc` if needed
- [x] Update `config/config_printer_test.cc` to test printing `session_capacity`

## Task 7: ControlPlane SessionTable ownership
- [x] Add `#include "session/session_table.h"` and `std::unique_ptr<session::SessionTable> session_table_` to `control/control_plane.h`
- [x] In `ControlPlane::Initialize()`, after RCU manager init: if `session_capacity > 0`, create and init SessionTable with rcu_manager_->GetQsbrVar()
- [x] Pass SessionTable pointer to thread manager or ProcessorContext before PMD thread launch
- [x] In `ControlPlane::Shutdown()`, destroy SessionTable after PMD threads stop but before RCU manager stop
- [x] Update `control/BUILD` to add `//session:session_table` dep to `control_plane`
- [x] Update root `BUILD` to add `//session:session_table` dep to main binary if needed

## Task 8: FiveTupleForwardingProcessor two-tier lookup
- [x] Add `session::SessionTable* session_table_ = nullptr` member to `FiveTupleForwardingProcessor`
- [x] Update `ExportProcessorData()` to read `ctx.session_table` and cast to `session::SessionTable*`
- [x] Update `process_impl()` with two-tier lookup: on L1 hit, validate version; on mismatch, clear and fall through to L2; on L1 miss, L2 lookup then insert; cache session pointer + version in LookupEntry
- [x] Ensure backward compatibility: when `session_table_` is nullptr, skip all session operations
- [x] Update `processor/BUILD` to add `//session:session_table` dep to `five_tuple_forwarding_processor`
- [x] Add `#include "session/session_table.h"` and `#include "session/session_entry.h"` to the processor .cc file
