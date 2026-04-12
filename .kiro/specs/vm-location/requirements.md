# Requirements Document

## Introduction

The VmLocation system provides a mapping from destination IP addresses to VXLAN tunnel endpoint IP addresses. When a packet's destination IP is looked up in the VmLocation table, the system returns the outer VXLAN destination IP (tunnel_info) used to encapsulate the packet. Multiple destination IPs may share the same tunnel endpoint, making the IndirectTable's value deduplication a natural fit. The VmLocation table supports both IPv4 and IPv6 addresses and follows the same ownership and lifecycle patterns as the existing SessionTable.

## Glossary

- **VmLocation_Table**: An IndirectTable instance that maps destination IP addresses (keys) to tunnel endpoint IP addresses (values). Owned by the ControlPlane as a unique_ptr.
- **Tunnel_Info**: A destination IP address representing the outer VXLAN tunnel endpoint. Stored as a value in the VmLocation_Table's SlotArray with reference-counted deduplication.
- **VmLocationKey**: A wrapper struct containing an IpAddress and an is_ipv6 flag, used as the key type for the VmLocation_Table. The flag disambiguates IPv4 and IPv6 since the raw IpAddress union does not carry type information.
- **IndirectTable**: A template-based dual hash table (key hash table + value SlotArray with reverse map) that supports key→value_id indirection with value deduplication via reference counting.
- **ControlPlane**: The main-lcore event loop orchestrator that owns shared resources (SessionTable, FIB tables, VmLocation_Table) and wires them into PMD threads and the CommandHandler.
- **ProcessorContext**: A per-PMD-thread struct carrying void* pointers to shared resources (session_table, lpm_table, tbm_table, vm_location_table) for lockless data-plane reads.
- **ConfigParser**: The JSON configuration file parser that populates DpdkConfig fields from the dpdk.json file.
- **DpdkConfig**: The top-level configuration struct holding all parsed JSON configuration values.
- **CommandHandler**: The control-plane command processor that handles JSON commands over the Unix socket, including table inspection commands.
- **RcuManager**: The Read-Copy-Update manager that provides grace-period-based deferred reclamation for lock-free data structures.

## Requirements

### Requirement 1: VmLocationKey Wrapper Type

**User Story:** As a developer, I want a key type that pairs an IpAddress with an is_ipv6 flag, so that the VmLocation_Table can correctly hash and compare keys for both IPv4 and IPv6 addresses.

#### Acceptance Criteria

1. THE VmLocationKey SHALL contain an IpAddress field and a bool is_ipv6 field.
2. WHEN is_ipv6 is false, THE VmLocationKey hash functor SHALL hash only the 4-byte v4 member of the IpAddress.
3. WHEN is_ipv6 is true, THE VmLocationKey hash functor SHALL hash all 16 bytes of the v6 member of the IpAddress.
4. WHEN is_ipv6 is false, THE VmLocationKey equality functor SHALL compare only the v4 members of two IpAddress values.
5. WHEN is_ipv6 is true, THE VmLocationKey equality functor SHALL compare all 16 bytes of the v6 members of two IpAddress values.
6. WHEN two VmLocationKey instances have different is_ipv6 values, THE VmLocationKey equality functor SHALL return false.

### Requirement 2: Tunnel_Info Value Type and Functors

**User Story:** As a developer, I want the tunnel endpoint value type to also carry an is_ipv6 flag alongside the IpAddress, so that the SlotArray reverse map can correctly deduplicate tunnel endpoints across address families.

#### Acceptance Criteria

1. THE Tunnel_Info type SHALL contain an IpAddress field and a bool is_ipv6 field.
2. WHEN is_ipv6 is false, THE Tunnel_Info hash functor SHALL hash only the 4-byte v4 member.
3. WHEN is_ipv6 is true, THE Tunnel_Info hash functor SHALL hash all 16 bytes of the v6 member.
4. WHEN two Tunnel_Info instances have the same is_ipv6 value and the same IP address bytes, THE Tunnel_Info equality functor SHALL return true.
5. WHEN two Tunnel_Info instances have different is_ipv6 values, THE Tunnel_Info equality functor SHALL return false.

### Requirement 3: VmLocation_Table Instantiation

**User Story:** As a developer, I want the VmLocation_Table to be an IndirectTable parameterized with VmLocationKey and Tunnel_Info, so that it provides key→value_id indirection with value deduplication for tunnel endpoints.

#### Acceptance Criteria

1. THE VmLocation_Table SHALL be an IndirectTable<VmLocationKey, Tunnel_Info, VmLocationKeyHash, VmLocationKeyEqual, TunnelInfoHash, TunnelInfoEqual>.
2. THE VmLocation_Table SHALL expose the IndirectTable API: Insert(key, value), InsertWithId(key, value_id), Remove(key), Find(key), and slot_array().Get(value_id).
3. THE VmLocation_Table Init method SHALL accept an IndirectTable::Config (value_capacity, value_bucket_count, key_capacity, key_bucket_count, name) and an RcuManager pointer.

### Requirement 4: DpdkConfig VmLocation Fields

**User Story:** As a system operator, I want to configure the VmLocation table capacities in the JSON config file, so that I can size the table appropriately for the deployment.

#### Acceptance Criteria

1. THE DpdkConfig SHALL contain a vm_location_value_capacity field of type uint32_t with a default value of 0.
2. THE DpdkConfig SHALL contain a vm_location_value_bucket_count field of type uint32_t with a default value of 0.
3. THE DpdkConfig SHALL contain a vm_location_key_capacity field of type uint32_t with a default value of 0.
4. THE DpdkConfig SHALL contain a vm_location_key_bucket_count field of type uint32_t with a default value of 0.
5. WHEN vm_location_value_capacity is 0, THE ControlPlane SHALL skip VmLocation_Table initialization.

### Requirement 5: ConfigParser VmLocation Parsing

**User Story:** As a system operator, I want the JSON config parser to read VmLocation fields from the config file, so that the VmLocation table is configured at startup.

#### Acceptance Criteria

1. WHEN the JSON config contains a "vm_location_value_capacity" field, THE ConfigParser SHALL parse the field as an unsigned integer into DpdkConfig::vm_location_value_capacity.
2. WHEN the JSON config contains a "vm_location_value_bucket_count" field, THE ConfigParser SHALL parse the field as an unsigned integer into DpdkConfig::vm_location_value_bucket_count.
3. WHEN the JSON config contains a "vm_location_key_capacity" field, THE ConfigParser SHALL parse the field as an unsigned integer into DpdkConfig::vm_location_key_capacity.
4. WHEN the JSON config contains a "vm_location_key_bucket_count" field, THE ConfigParser SHALL parse the field as an unsigned integer into DpdkConfig::vm_location_key_bucket_count.
5. WHEN a vm_location field is present but is not an unsigned integer, THE ConfigParser SHALL return an InvalidArgument error with a descriptive message.
6. WHEN the vm_location fields are absent from the JSON config, THE ConfigParser SHALL use the default values of 0.

### Requirement 6: ControlPlane VmLocation Ownership and Initialization

**User Story:** As a developer, I want the ControlPlane to own and initialize the VmLocation_Table following the same pattern as the SessionTable, so that the table lifecycle is managed consistently.

#### Acceptance Criteria

1. THE ControlPlane SHALL own the VmLocation_Table as a std::unique_ptr.
2. THE ControlPlane::Config SHALL contain vm_location_value_capacity, vm_location_value_bucket_count, vm_location_key_capacity, and vm_location_key_bucket_count fields.
3. WHEN vm_location_value_capacity is greater than 0, THE ControlPlane::Initialize method SHALL create and initialize the VmLocation_Table with the configured capacities and the RcuManager pointer.
4. IF VmLocation_Table initialization fails, THEN THE ControlPlane::Initialize method SHALL return the error status and skip further VmLocation setup.
5. WHEN the ControlPlane shuts down, THE ControlPlane SHALL destroy the VmLocation_Table after all PMD threads have stopped.

### Requirement 7: ProcessorContext VmLocation Wiring

**User Story:** As a developer, I want the VmLocation_Table pointer to be available in each PMD thread's ProcessorContext, so that packet processors can perform lockless lookups during data-plane processing.

#### Acceptance Criteria

1. THE ProcessorContext SHALL contain a void* vm_location_table field initialized to nullptr.
2. WHEN the VmLocation_Table is initialized, THE ControlPlane SHALL set the vm_location_table field in each PMD thread's ProcessorContext to point to the VmLocation_Table instance.
3. WHEN vm_location_value_capacity is 0, THE ProcessorContext::vm_location_table field SHALL remain nullptr.

### Requirement 8: CommandHandler VmLocation Integration

**User Story:** As a system operator, I want control-plane commands to inspect the VmLocation table, so that I can monitor and debug tunnel endpoint mappings at runtime.

#### Acceptance Criteria

1. THE CommandHandler SHALL provide a SetVmLocationTable method that accepts a VmLocation_Table pointer.
2. WHEN the VmLocation_Table is initialized, THE ControlPlane SHALL call SetVmLocationTable on the CommandHandler with the VmLocation_Table pointer.
3. WHEN SetVmLocationTable is called with a non-null pointer, THE CommandHandler SHALL register a "get_vm_locations" sync command under the "vm_location" tag.
4. WHEN the "get_vm_locations" command is invoked, THE CommandHandler SHALL iterate all key entries in the VmLocation_Table and return each mapping as a JSON object containing the destination IP (formatted as a string), the tunnel endpoint IP (formatted as a string), the is_ipv6 flag, and the value_id.
