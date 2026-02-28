# Design Document: PMD Thread Configuration

## Overview

This design extends the existing DPDK configuration system to support PMD (Poll Mode Driver) thread configuration. The implementation adds new data structures, parsing logic, validation rules, and serialization support for configuring which CPU cores run PMD threads and which port/queue pairs each thread handles.

## Architecture

### Component Overview

The feature extends four existing components and adds two new classes:

1. **Data Structures** (`config/dpdk_config.h`): Add PMD thread configuration structures
2. **Config Parser** (`config/config_parser.cc`): Parse PMD thread configuration from JSON
3. **Config Validator** (`config/config_validator.cc`): Validate PMD thread assignments
4. **Config Printer** (`config/config_printer.cc`): Serialize PMD thread configuration to JSON
5. **PMDThreadManager** (`config/pmd_thread_manager.h/cc`): Manages lifecycle and coordination of all PMD threads
6. **PMDThread** (`config/pmd_thread.h/cc`): Encapsulates per-thread logic and packet processing

### Architecture Pattern

This design follows the same pattern as the existing PortManager/DpdkPort architecture:

- **Manager Class (PMDThreadManager)**: Coordinates multiple instances, handles initialization and lifecycle
- **Instance Class (PMDThread)**: Encapsulates per-instance state and operations

This separation provides:
- **Single Responsibility**: Manager handles coordination, instances handle execution
- **Testability**: Each class can be tested independently
- **Extensibility**: Easy to add new thread types or management strategies

### Data Flow

```
JSON Config → ConfigParser → PmdThreadConfig (vector)
                                     ↓
                              ConfigValidator
                                     ↓
                              PMDThreadManager::LaunchThreads
                                     ↓
                    ┌────────────────┴────────────────┐
                    ↓                                 ↓
              PMDThread(config1)              PMDThread(config2)
                    ↓                                 ↓
         rte_eal_remote_launch(lcore1)  rte_eal_remote_launch(lcore2)
                    ↓                                 ↓
              PMDThread::Run()                 PMDThread::Run()
           (polls port0, queue0)            (polls port1, queue0)
```

Each PMDThread receives its configuration (lcore_id + queue assignments) and runs independently on its assigned lcore.

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

## Class Designs

### PMDThread Class

**Location**: `config/pmd_thread.h`, `config/pmd_thread.cc`

**Purpose**: Encapsulates per-thread logic for a single PMD worker thread.

**Responsibilities**:
- Store (port, queue_id) pairs for RX and TX operations
- Execute the packet processing loop on assigned lcore
- Provide thread-local operations and state management

**Interface**:

```cpp
class PMDThread {
 public:
  // Create a PMD thread from configuration
  explicit PMDThread(const PmdThreadConfig& config);
  
  // Get the lcore ID this thread runs on
  uint32_t GetLcoreId() const { return config_.lcore_id; }
  
  // Get RX queue assignments
  const std::vector<QueueAssignment>& GetRxQueues() const { 
    return config_.rx_queues; 
  }
  
  // Get TX queue assignments
  const std::vector<QueueAssignment>& GetTxQueues() const { 
    return config_.tx_queues; 
  }
  
  // Static entry point for DPDK remote launch
  // This is the function passed to rte_eal_remote_launch
  static int RunStub(void* arg);
  
 private:
  // The actual packet processing loop (stub implementation)
  int Run();
  
  PmdThreadConfig config_;
};
```

**Implementation Notes**:
- Constructor stores the configuration (lcore_id and queue assignments)
- `RunStub` is a static function that casts the void* argument back to PMDThread* and calls Run()
- `Run()` is the instance method that executes the packet processing loop
- For now, Run() is a stub that logs the lcore and queue assignments, then returns immediately
- Future implementations will add actual packet polling logic using rte_eth_rx_burst/rte_eth_tx_burst

### PMDThreadManager Class

**Location**: `config/pmd_thread_manager.h`, `config/pmd_thread_manager.cc`

**Purpose**: Manages lifecycle and coordination of all PMD threads.

**Responsibilities**:
- Launch all PMD threads based on configuration
- Track thread handles and state
- Coordinate shutdown sequence (stop, wait, join operations)
- Provide access to individual thread instances

**Interface**:

```cpp
class PMDThreadManager {
 public:
  PMDThreadManager() = default;
  
  // Initialize and launch all PMD threads from configuration
  // Must be called after rte_eal_init()
  // Skips the main lcore (reserved for control plane)
  absl::Status LaunchThreads(const std::vector<PmdThreadConfig>& thread_configs);
  
  // Wait for all PMD threads to complete
  // Calls rte_eal_wait_lcore for each launched thread
  absl::Status WaitForThreads();
  
  // Get a specific thread by lcore ID
  PMDThread* GetThread(uint32_t lcore_id);
  
  // Get all lcore IDs with running threads
  std::vector<uint32_t> GetLcoreIds() const;
  
  // Get number of launched threads
  size_t GetThreadCount() const { return threads_.size(); }
  
 private:
  std::unordered_map<uint32_t, std::unique_ptr<PMDThread>> threads_;
};
```

**Implementation Notes**:
- `LaunchThreads` creates PMDThread instances and calls rte_eal_remote_launch for each
- Each thread is launched on its configured lcore using `rte_eal_remote_launch(PMDThread::RunStub, thread.get(), lcore_id)`
- Main lcore is automatically skipped (validation ensures no config uses main lcore)
- `WaitForThreads` calls `rte_eal_wait_lcore` for each launched lcore
- Threads are stored in a map keyed by lcore_id for easy lookup
- Manager owns the PMDThread instances via unique_ptr

**Launch Algorithm**:

```
FUNCTION LaunchThreads(thread_configs: vector<PmdThreadConfig>) -> Status
  // Clear any existing threads
  threads_.clear()
  
  FOR EACH config IN thread_configs DO
    // Create PMDThread instance
    thread = make_unique<PMDThread>(config)
    lcore_id = config.lcore_id
    
    // Launch thread on remote lcore
    ret = rte_eal_remote_launch(PMDThread::RunStub, thread.get(), lcore_id)
    IF ret != 0 THEN
      RETURN error "Failed to launch PMD thread on lcore {lcore_id}"
    END IF
    
    // Store thread in map
    threads_[lcore_id] = move(thread)
  END FOR
  
  RETURN OK
END FUNCTION

FUNCTION WaitForThreads() -> Status
  FOR EACH [lcore_id, thread] IN threads_ DO
    ret = rte_eal_wait_lcore(lcore_id)
    IF ret != 0 THEN
      RETURN error "PMD thread on lcore {lcore_id} returned error: {ret}"
    END IF
  END FOR
  
  RETURN OK
END FUNCTION
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

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Configuration Properties

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

### Thread Management Properties

### Property 10: Thread-Config Correspondence

**Statement**: For any valid configuration, PMDThreadManager creates exactly one PMDThread per PmdThreadConfig.

**Formal Definition**:
```
∀ thread_configs ∈ ValidPmdThreadConfigs:
  manager.LaunchThreads(thread_configs) succeeds
  ⟹ manager.GetThreadCount() = |thread_configs|
  ∧ ∀ config ∈ thread_configs: 
      manager.GetThread(config.lcore_id) ≠ null
```

**Validates**: Requirement 5.1

**Test Strategy**: Property-based test with various configurations, verify thread count and lookup

### Property 11: Thread Configuration Preservation

**Statement**: Each PMDThread preserves its configuration exactly as provided.

**Formal Definition**:
```
∀ config ∈ ValidPmdThreadConfig:
  thread = PMDThread(config)
  ⟹ thread.GetLcoreId() = config.lcore_id
  ∧ thread.GetRxQueues() = config.rx_queues
  ∧ thread.GetTxQueues() = config.tx_queues
```

**Validates**: Requirements 4.1, 4.2, 4.3, 4.4, 4.5

**Test Strategy**: Property-based test generating random configs, verify accessors return exact values

### Property 12: Launch-Wait Correspondence

**Statement**: For any successfully launched threads, WaitForThreads completes for all lcores.

**Formal Definition**:
```
∀ thread_configs ∈ ValidPmdThreadConfigs:
  manager.LaunchThreads(thread_configs) succeeds
  ⟹ manager.WaitForThreads() succeeds
  ∧ ∀ config ∈ thread_configs:
      rte_eal_wait_lcore(config.lcore_id) was called
```

**Validates**: Requirement 5.5

**Test Strategy**: Integration test with DPDK EAL, verify wait is called for each lcore

## Implementation Notes

### Two-Class Architecture Benefits

The PMDThreadManager/PMDThread split provides several advantages:

1. **Separation of Concerns**:
   - PMDThread focuses on per-thread execution logic
   - PMDThreadManager handles coordination and lifecycle

2. **Testability**:
   - PMDThread can be unit tested without DPDK EAL initialization
   - PMDThreadManager can be tested with mock PMDThread instances

3. **Consistency**:
   - Mirrors the existing PortManager/DpdkPort pattern
   - Developers familiar with port management will understand thread management

4. **Extensibility**:
   - Easy to add new thread types (e.g., control threads, monitoring threads)
   - Manager can implement different launch strategies (sequential, parallel, priority-based)

### Thread Launch Sequence

The typical usage pattern:

```cpp
// After rte_eal_init() and port initialization
PMDThreadManager thread_manager;

// Launch all configured PMD threads
absl::Status status = thread_manager.LaunchThreads(config.pmd_threads);
if (!status.ok()) {
  // Handle error
}

// Main lcore continues with control plane work (boost.asio)
// ...

// When shutting down, wait for all threads
status = thread_manager.WaitForThreads();
```

### Main Lcore Reservation

The main lcore (returned by `rte_get_main_lcore()`) is reserved for control plane operations:
- Runs the boost.asio unix-socket listener
- Handles control requests without blocking on packet processing
- Never assigned to PMD threads (enforced by validation)

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

## Error Handling

### Configuration Errors

Configuration parsing and validation errors are returned as `absl::Status` with descriptive messages:

- **Parse Errors**: Invalid JSON structure, missing required fields, wrong types
  - Example: "Field 'lcore_id' must be an unsigned integer"
  - Example: "PMD thread missing required field: lcore_id"

- **Validation Errors**: Constraint violations in valid JSON
  - Example: "PMD thread cannot use main lcore 0 (reserved for control plane)"
  - Example: "Duplicate RX queue assignment: port 0, queue 1"
  - Example: "PMD thread on lcore 2: RX queue 3 out of range for port 0 (max: 1)"

### Thread Management Errors

Thread launch and lifecycle errors:

- **Launch Failures**: DPDK remote launch errors
  - Example: "Failed to launch PMD thread on lcore 2: lcore not available"
  - Returned immediately from `PMDThreadManager::LaunchThreads`
  - Cleanup: Any successfully launched threads remain running (caller must handle)

- **Wait Failures**: Thread execution errors
  - Example: "PMD thread on lcore 2 returned error: -1"
  - Returned from `PMDThreadManager::WaitForThreads`
  - Indicates thread encountered an error during execution

### Error Recovery

- **Configuration Errors**: Fix configuration and retry parsing/validation
- **Launch Errors**: Check DPDK EAL initialization and coremask, retry launch
- **Wait Errors**: Investigate thread logs, check for resource exhaustion

## Testing Strategy

### Unit Testing

**PMDThread Tests**:
- Constructor stores configuration correctly
- Accessors return correct values (lcore_id, rx_queues, tx_queues)
- Can be tested without DPDK EAL initialization

**PMDThreadManager Tests**:
- LaunchThreads creates correct number of threads
- GetThread returns correct thread by lcore_id
- GetLcoreIds returns all launched lcores
- Can use mock PMDThread instances for testing without DPDK

**Configuration Tests**:
- Parser handles valid and invalid JSON
- Validator catches all constraint violations
- Printer produces valid JSON
- Round-trip consistency

### Property-Based Testing

Using a C++ property-based testing library (e.g., RapidCheck):

- Generate random valid configurations (100+ iterations per property)
- Verify all 12 correctness properties
- Tag each test with: **Feature: pmd-thread-config, Property N: [description]**

### Integration Testing

**With DPDK EAL**:
- Launch real PMDThread instances on worker lcores
- Verify threads execute and return successfully
- Test WaitForThreads completes for all threads
- Requires multi-core test environment

**End-to-End**:
- Parse config → Validate → Launch threads → Wait → Verify execution
- Test with various coremasks and queue configurations

### Edge Cases

- Empty pmd_threads array (valid, no threads launched)
- Single worker lcore (main + 1 worker)
- Maximum queue counts (stress test)
- Main lcore boundary (lcore 0 vs. other main lcores)
- All queues assigned vs. partial assignment

### Test Organization

```
config/
  pmd_thread_test.cc              # PMDThread unit tests
  pmd_thread_manager_test.cc      # PMDThreadManager unit tests
  config_parser_test.cc           # Parser tests (extended)
  config_validator_test.cc        # Validator tests (extended)
  config_printer_test.cc          # Printer tests (extended)
  pmd_thread_properties_test.cc   # Property-based tests
  pmd_thread_integration_test.cc  # Integration tests (requires DPDK)
```

## Dependencies

- Existing DPDK configuration system (dpdk_config.h, config_parser, config_validator, config_printer)
- Existing PortManager/DpdkPort architecture (pattern to mirror)
- nlohmann/json library for JSON parsing
- absl::Status for error handling
- C++17 standard library (std::optional, std::vector, std::unordered_map, std::unique_ptr)
- DPDK EAL APIs (rte_eal_remote_launch, rte_eal_wait_lcore, rte_get_main_lcore)

## Future Extensions

This design implements the complete infrastructure for PMD thread management:
- Configuration parsing, validation, and serialization ✓
- PMDThread class for per-thread logic ✓
- PMDThreadManager class for lifecycle management ✓
- Thread launch and coordination ✓

Future work will include:
- Actual packet processing implementation (replacing the stub in PMDThread::Run)
- Packet RX/TX operations using rte_eth_rx_burst/rte_eth_tx_burst
- Performance monitoring and statistics collection per thread
- Dynamic thread reconfiguration (add/remove threads at runtime)
- Thread affinity and priority tuning
