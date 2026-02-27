# Design Document: PMD Thread Configuration

## Overview

This design extends the existing DPDK configuration system to support PMD (Poll Mode Driver) thread configuration. The implementation adds new data structures, parsing logic, validation rules, and serialization support for configuring which CPU cores run PMD threads and which port/queue pairs each thread handles.

## Architecture

### Component Overview

The feature extends four existing components:

1. **Data Structures** (`config/dpdk_config.h`): Add PMD thread configuration structures
2. **Config Parser** (`config/config_parser.cc`): Parse PMD thread configuration from JSON
3. **Config Validator** (`config/config_validator.cc`): Validate PMD thread assignments
4. **Config Printer** (`config/config_printer.cc`): Serialize PMD thread configuration to JSON

### Data Structure Design

#### QueueAssignment Structure

```cpp
struct QueueAssignment {
  uint16_t port_id;
  uint16_t queue_id;
};
```

Represents a single (port, queue) pair assignment.

#### PmdThreadConfig Structure

```cpp
struct PmdThreadConfig {
  uint32_t lcore_id;
  std::vector<QueueAssignment> rx_queues;
  std::vector<QueueAssignment> tx_queues;
};
```

Represents the configuration for a single PMD thread, including:
- The lcore (CPU core) on which the thread runs
- List of RX queue assignments
- List of TX queue assignments

#### DpdkConfig Extension

Add to existing `DpdkConfig` structure:

```cpp
struct DpdkConfig {
  // ... existing fields ...
  
  // PMD thread configurations
  std::vector<PmdThreadConfig> pmd_threads;
};
```

## JSON Schema

### Input Format

```json
{
  "core_mask": "0xff",
  "ports": [...],
  "pmd_threads": [
    {
      "lcore_id": 1,
      "rx_queues": [
        {"port_id": 0, "queue_id": 0},
        {"port_id": 0, "queue_id": 1}
      ],
      "tx_queues": [
        {"port_id": 0, "queue_id": 0},
        {"port_id": 0, "queue_id": 1}
      ]
    },
    {
      "lcore_id": 2,
      "rx_queues": [
        {"port_id": 1, "queue_id": 0}
      ],
      "tx_queues": [
        {"port_id": 1, "queue_id": 0}
      ]
    }
  ]
}
```

## Component Designs

### Config Parser Extension

**Location**: `config/config_parser.cc`

**Function**: `ConfigParser::ParseString`

**Algorithm**:

```
FUNCTION ParseString(json_content: string) -> StatusOr<DpdkConfig>
  // ... existing parsing logic ...
  
  // Parse pmd_threads array (optional)
  IF json contains "pmd_threads" THEN
    IF json["pmd_threads"] is not array THEN
      RETURN error "Field 'pmd_threads' must be an array"
    END IF
    
    FOR EACH thread_json IN json["pmd_threads"] DO
      IF thread_json is not object THEN
        RETURN error "Each element in 'pmd_threads' must be an object"
      END IF
      
      pmd_config = PmdThreadConfig{}
      
      // Parse lcore_id (required)
      IF thread_json does not contain "lcore_id" THEN
        RETURN error "PMD thread missing required field: lcore_id"
      END IF
      IF thread_json["lcore_id"] is not unsigned integer THEN
        RETURN error "Field 'lcore_id' must be an unsigned integer"
      END IF
      pmd_config.lcore_id = thread_json["lcore_id"]
      
      // Parse rx_queues (optional array)
      IF thread_json contains "rx_queues" THEN
        IF thread_json["rx_queues"] is not array THEN
          RETURN error "Field 'rx_queues' must be an array"
        END IF
        
        FOR EACH queue_json IN thread_json["rx_queues"] DO
          queue = ParseQueueAssignment(queue_json, pmd_config.lcore_id)
          IF queue is error THEN
            RETURN queue.error
          END IF
          pmd_config.rx_queues.push_back(queue)
        END FOR
      END IF
      
      // Parse tx_queues (optional array)
      IF thread_json contains "tx_queues" THEN
        IF thread_json["tx_queues"] is not array THEN
          RETURN error "Field 'tx_queues' must be an array"
        END IF
        
        FOR EACH queue_json IN thread_json["tx_queues"] DO
          queue = ParseQueueAssignment(queue_json, pmd_config.lcore_id)
          IF queue is error THEN
            RETURN queue.error
          END IF
          pmd_config.tx_queues.push_back(queue)
        END FOR
      END IF
      
      config.pmd_threads.push_back(pmd_config)
    END FOR
  END IF
  
  RETURN config
END FUNCTION

FUNCTION ParseQueueAssignment(queue_json: json, lcore_id: uint32) -> StatusOr<QueueAssignment>
  IF queue_json is not object THEN
    RETURN error "Queue assignment must be an object"
  END IF
  
  // Parse port_id (required)
  IF queue_json does not contain "port_id" THEN
    RETURN error "Queue assignment for lcore {lcore_id} missing required field: port_id"
  END IF
  IF queue_json["port_id"] is not unsigned integer THEN
    RETURN error "Field 'port_id' must be an unsigned integer"
  END IF
  
  // Parse queue_id (required)
  IF queue_json does not contain "queue_id" THEN
    RETURN error "Queue assignment for lcore {lcore_id} missing required field: queue_id"
  END IF
  IF queue_json["queue_id"] is not unsigned integer THEN
    RETURN error "Field 'queue_id' must be an unsigned integer"
  END IF
  
  assignment = QueueAssignment{
    port_id: queue_json["port_id"],
    queue_id: queue_json["queue_id"]
  }
  
  RETURN assignment
END FUNCTION
```

### Config Validator Extension

**Location**: `config/config_validator.cc`

**Function**: `ConfigValidator::Validate`

**Algorithm**:

```
FUNCTION Validate(config: DpdkConfig) -> Status
  // ... existing validation logic ...
  
  // Validate PMD thread configurations
  IF config.pmd_threads is not empty THEN
    // Get main lcore (requires DPDK EAL initialization)
    // For validation without EAL, we assume lcore 0 is main if core_mask starts with bit 0
    main_lcore = DetermineMainLcore(config.core_mask)
    
    // Get available lcores from coremask
    available_lcores = ParseCoremask(config.core_mask)
    
    // Check if there are worker lcores available
    worker_lcores = available_lcores - {main_lcore}
    IF worker_lcores is empty THEN
      RETURN error "No worker lcores available (coremask only contains main lcore)"
    END IF
    
    seen_lcores = empty set
    seen_rx_queues = empty set of (port_id, queue_id) pairs
    seen_tx_queues = empty set of (port_id, queue_id) pairs
    
    FOR EACH pmd_config IN config.pmd_threads DO
      lcore = pmd_config.lcore_id
      
      // Check if lcore is the main lcore
      IF lcore == main_lcore THEN
        RETURN error "PMD thread cannot use main lcore {lcore} (reserved for control plane)"
      END IF
      
      // Check if lcore is in coremask
      IF lcore not in available_lcores THEN
        RETURN error "PMD thread lcore {lcore} is not in coremask"
      END IF
      
      // Check for duplicate lcore assignments
      IF lcore in seen_lcores THEN
        RETURN error "Duplicate lcore assignment: {lcore}"
      END IF
      seen_lcores.add(lcore)
      
      // Validate RX queue assignments
      FOR EACH queue IN pmd_config.rx_queues DO
        // Check if port exists
        port = FindPort(config.ports, queue.port_id)
        IF port is null THEN
          RETURN error "PMD thread on lcore {lcore}: unknown port {queue.port_id}"
        END IF
        
        // Check if queue_id is in range
        IF queue.queue_id >= port.num_rx_queues THEN
          RETURN error "PMD thread on lcore {lcore}: RX queue {queue.queue_id} out of range for port {queue.port_id} (max: {port.num_rx_queues - 1})"
        END IF
        
        // Check for duplicate queue assignments
        queue_pair = (queue.port_id, queue.queue_id)
        IF queue_pair in seen_rx_queues THEN
          RETURN error "Duplicate RX queue assignment: port {queue.port_id}, queue {queue.queue_id}"
        END IF
        seen_rx_queues.add(queue_pair)
      END FOR
      
      // Validate TX queue assignments
      FOR EACH queue IN pmd_config.tx_queues DO
        // Check if port exists
        port = FindPort(config.ports, queue.port_id)
        IF port is null THEN
          RETURN error "PMD thread on lcore {lcore}: unknown port {queue.port_id}"
        END IF
        
        // Check if queue_id is in range
        IF queue.queue_id >= port.num_tx_queues THEN
          RETURN error "PMD thread on lcore {lcore}: TX queue {queue.queue_id} out of range for port {queue.port_id} (max: {port.num_tx_queues - 1})"
        END IF
        
        // Check for duplicate queue assignments
        queue_pair = (queue.port_id, queue.queue_id)
        IF queue_pair in seen_tx_queues THEN
          RETURN error "Duplicate TX queue assignment: port {queue.port_id}, queue {queue.queue_id}"
        END IF
        seen_tx_queues.add(queue_pair)
      END FOR
    END FOR
  END IF
  
  RETURN OK
END FUNCTION

FUNCTION DetermineMainLcore(core_mask: optional<string>) -> uint32
  // Without EAL initialization, we use heuristic:
  // Main lcore is the lowest-numbered core in the coremask
  IF core_mask is empty THEN
    RETURN 0  // Default assumption
  END IF
  
  lcores = ParseCoremask(core_mask)
  IF lcores is empty THEN
    RETURN 0
  END IF
  
  RETURN min(lcores)
END FUNCTION

FUNCTION ParseCoremask(core_mask: optional<string>) -> set<uint32>
  IF core_mask is empty THEN
    RETURN empty set
  END IF
  
  // Remove 0x prefix if present
  hex_str = core_mask
  IF hex_str starts with "0x" or "0X" THEN
    hex_str = hex_str[2:]
  END IF
  
  // Convert hex string to integer
  mask_value = parse_hex(hex_str)
  
  // Extract bit positions
  lcores = empty set
  FOR bit_position FROM 0 TO 63 DO
    IF (mask_value & (1 << bit_position)) != 0 THEN
      lcores.add(bit_position)
    END IF
  END FOR
  
  RETURN lcores
END FUNCTION

FUNCTION FindPort(ports: vector<DpdkPortConfig>, port_id: uint16) -> DpdkPortConfig*
  FOR EACH port IN ports DO
    IF port.port_id == port_id THEN
      RETURN &port
    END IF
  END FOR
  RETURN null
END FUNCTION
```

### Config Printer Extension

**Location**: `config/config_printer.cc`

**Function**: `ConfigPrinter::ToJson`

**Algorithm**:

```
FUNCTION ToJson(config: DpdkConfig, indent: int) -> string
  json_obj = empty JSON object
  
  // ... existing serialization logic ...
  
  // Serialize pmd_threads (array of PMD thread configurations)
  IF config.pmd_threads is not empty THEN
    pmd_threads_array = empty JSON array
    
    FOR EACH pmd_config IN config.pmd_threads DO
      thread_json = empty JSON object
      thread_json["lcore_id"] = pmd_config.lcore_id
      
      // Serialize rx_queues
      IF pmd_config.rx_queues is not empty THEN
        rx_queues_array = empty JSON array
        FOR EACH queue IN pmd_config.rx_queues DO
          queue_json = empty JSON object
          queue_json["port_id"] = queue.port_id
          queue_json["queue_id"] = queue.queue_id
          rx_queues_array.push_back(queue_json)
        END FOR
        thread_json["rx_queues"] = rx_queues_array
      END IF
      
      // Serialize tx_queues
      IF pmd_config.tx_queues is not empty THEN
        tx_queues_array = empty JSON array
        FOR EACH queue IN pmd_config.tx_queues DO
          queue_json = empty JSON object
          queue_json["port_id"] = queue.port_id
          queue_json["queue_id"] = queue.queue_id
          tx_queues_array.push_back(queue_json)
        END FOR
        thread_json["tx_queues"] = tx_queues_array
      END IF
      
      pmd_threads_array.push_back(thread_json)
    END FOR
    
    json_obj["pmd_threads"] = pmd_threads_array
  END IF
  
  RETURN json_obj.dump(indent)
END FUNCTION
```

## Correctness Properties

### Property 1: Parse-Print Round Trip Consistency

**Statement**: For any valid PMD thread configuration JSON, parsing and then printing produces an equivalent configuration.

**Formal Definition**:
```
∀ json_str ∈ ValidPmdThreadJson:
  config = Parse(json_str)
  json_str' = Print(config)
  config' = Parse(json_str')
  ⟹ config.pmd_threads ≡ config'.pmd_threads
```

**Validates**: Requirements 1.1-1.8, 3.1-3.6

**Test Strategy**: Property-based test generating random valid PMD thread configurations

### Property 2: Main Lcore Exclusion

**Statement**: No PMD thread configuration can use the main lcore.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  main_lcore = GetMainLcore(config.core_mask)
  ⟹ ∀ pmd ∈ config.pmd_threads: pmd.lcore_id ≠ main_lcore
```

**Validates**: Requirements 2.1, 7.1, 7.2

**Test Strategy**: Property-based test with various coremasks, verify validation rejects main lcore

### Property 3: Lcore Uniqueness

**Statement**: Each lcore can be assigned to at most one PMD thread.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ i, j ∈ [0, |config.pmd_threads|): i ≠ j
  ⟹ config.pmd_threads[i].lcore_id ≠ config.pmd_threads[j].lcore_id
```

**Validates**: Requirement 2.3

**Test Strategy**: Property-based test generating configurations, verify validation rejects duplicates

### Property 4: Queue Assignment Uniqueness (RX)

**Statement**: Each RX queue can be assigned to at most one PMD thread.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ pmd_i, pmd_j ∈ config.pmd_threads:
    ∀ q_i ∈ pmd_i.rx_queues, q_j ∈ pmd_j.rx_queues:
      (pmd_i ≠ pmd_j ∨ q_i ≠ q_j)
      ⟹ (q_i.port_id, q_i.queue_id) ≠ (q_j.port_id, q_j.queue_id)
```

**Validates**: Requirement 2.4

**Test Strategy**: Property-based test generating queue assignments, verify validation rejects duplicates

### Property 5: Queue Assignment Uniqueness (TX)

**Statement**: Each TX queue can be assigned to at most one PMD thread.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ pmd_i, pmd_j ∈ config.pmd_threads:
    ∀ q_i ∈ pmd_i.tx_queues, q_j ∈ pmd_j.tx_queues:
      (pmd_i ≠ pmd_j ∨ q_i ≠ q_j)
      ⟹ (q_i.port_id, q_i.queue_id) ≠ (q_j.port_id, q_j.queue_id)
```

**Validates**: Requirement 2.5

**Test Strategy**: Property-based test generating queue assignments, verify validation rejects duplicates

### Property 6: Port Reference Validity

**Statement**: All port IDs in queue assignments must reference configured ports.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ pmd ∈ config.pmd_threads:
    ∀ q ∈ (pmd.rx_queues ∪ pmd.tx_queues):
      ∃ port ∈ config.ports: port.port_id = q.port_id
```

**Validates**: Requirement 2.6

**Test Strategy**: Property-based test with various port configurations, verify validation rejects unknown ports

### Property 7: Queue Range Validity (RX)

**Statement**: All RX queue IDs must be within the configured range for their port.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ pmd ∈ config.pmd_threads:
    ∀ q ∈ pmd.rx_queues:
      port = FindPort(config.ports, q.port_id)
      ⟹ q.queue_id < port.num_rx_queues
```

**Validates**: Requirement 2.7

**Test Strategy**: Property-based test generating queue IDs, verify validation rejects out-of-range values

### Property 8: Queue Range Validity (TX)

**Statement**: All TX queue IDs must be within the configured range for their port.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  ∀ pmd ∈ config.pmd_threads:
    ∀ q ∈ pmd.tx_queues:
      port = FindPort(config.ports, q.port_id)
      ⟹ q.queue_id < port.num_tx_queues
```

**Validates**: Requirement 2.8

**Test Strategy**: Property-based test generating queue IDs, verify validation rejects out-of-range values

### Property 9: Lcore Availability

**Statement**: All PMD thread lcores must be present in the coremask.

**Formal Definition**:
```
∀ config ∈ ValidDpdkConfig:
  available_lcores = ParseCoremask(config.core_mask)
  ⟹ ∀ pmd ∈ config.pmd_threads: pmd.lcore_id ∈ available_lcores
```

**Validates**: Requirement 2.2

**Test Strategy**: Property-based test with various coremasks, verify validation rejects unavailable lcores

## Implementation Notes

### Coremask Parsing

The validator needs to parse hexadecimal coremask strings to determine available lcores. Implementation should:
- Handle optional "0x" prefix
- Support both uppercase and lowercase hex digits
- Extract bit positions to determine lcore IDs
- Handle up to 64-bit masks (supporting up to 64 cores)

### Main Lcore Determination

Without DPDK EAL initialization, the validator uses a heuristic:
- Main lcore is the lowest-numbered bit set in the coremask
- This matches DPDK's default behavior when no explicit main lcore is specified

### Error Messages

All validation errors should include:
- The specific lcore_id or port/queue pair that caused the error
- Clear indication of what constraint was violated
- Actionable guidance for fixing the configuration

### Testing Strategy

1. **Unit Tests**: Test each parsing/validation/printing function independently
2. **Property-Based Tests**: Generate random valid configurations and verify properties
3. **Integration Tests**: Test complete parse-validate-print cycles
4. **Edge Cases**: Empty arrays, single-core systems, maximum queue counts

## Dependencies

- Existing DPDK configuration system (dpdk_config.h, config_parser, config_validator, config_printer)
- nlohmann/json library for JSON parsing
- absl::Status for error handling
- C++17 standard library (std::optional, std::vector)

## Future Extensions

This design focuses on configuration parsing, validation, and serialization. Future work will include:
- PMD thread lifecycle management (launching threads on lcores)
- Packet processing loop implementation
- Integration with DPDK rte_eal_remote_launch API
- Performance monitoring and statistics collection
