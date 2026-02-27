# Design Document: DPDK JSON Configuration

## Overview

This design document specifies the architecture and implementation approach for adding JSON-based configuration file support to a DPDK application. The feature allows users to specify DPDK Environment Abstraction Layer (EAL) initialization parameters through a JSON configuration file instead of command line arguments.

The design integrates with the existing Abseil flags system and maintains backward compatibility with direct command line argument passing to DPDK. The implementation follows a modular architecture with clear separation between parsing, validation, and initialization concerns.

### Key Design Goals

- Minimal changes to existing application structure
- Clear error reporting for configuration issues
- Type-safe configuration data structures
- Round-trip capability (parse → print → parse preserves data)
- Integration with existing verbose flag for debugging

## Architecture

### Component Overview

The system consists of four primary components:

```
┌─────────────────┐
│   main.cc       │
│  (Entry Point)  │
└────────┬────────┘
         │
         ├──────────────────┐
         │                  │
         ▼                  ▼
┌─────────────────┐  ┌──────────────────┐
│  Config Parser  │  │  Abseil Flags    │
│  (JSON → Data)  │  │  (CLI Parsing)   │
└────────┬────────┘  └──────────────────┘
         │
         ▼
┌─────────────────┐
│ Config Validator│
│ (Data Checking) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ DPDK Initializer│
│ (Data → Args)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  rte_eal_init() │
│  (DPDK Runtime) │
└─────────────────┘
```

### Data Flow

1. Application starts, Abseil parses command line flags
2. If `-i` flag is present, Config Parser reads and parses JSON file
3. Config Validator checks parsed data for correctness
4. DPDK Initializer constructs argc/argv for rte_eal_init()
5. DPDK initializes with constructed arguments
6. Application continues normal execution

### Module Organization

```
project_root/
├── main.cc                    # Entry point, flag definitions
├── config/
│   ├── config_parser.h        # JSON parsing interface
│   ├── config_parser.cc       # JSON parsing implementation
│   ├── config_validator.h     # Validation interface
│   ├── config_validator.cc    # Validation implementation
│   ├── config_printer.h       # JSON serialization interface
│   ├── config_printer.cc      # JSON serialization implementation
│   ├── dpdk_config.h          # Configuration data structures
│   └── dpdk_initializer.h     # DPDK initialization logic
│       dpdk_initializer.cc
└── BUILD                      # Bazel build configuration
```

## Components and Interfaces

### Configuration Data Structure

The core data structure represents all supported EAL parameters and port configurations:

```cpp
// config/dpdk_config.h
#ifndef CONFIG_DPDK_CONFIG_H_
#define CONFIG_DPDK_CONFIG_H_

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace dpdk_config {

// Port configuration structure for DPDK port initialization
struct DpdkPortConfig {
  // Port ID (required, must be unique)
  uint16_t port_id;
  
  // Number of RX queues (required, must be > 0)
  uint16_t num_rx_queues;
  
  // Number of TX queues (required, must be > 0)
  uint16_t num_tx_queues;
  
  // Number of descriptors per RX/TX queue (required, must be power of 2)
  uint16_t num_descriptors;
  
  // Mbuf pool size - total number of mbufs in the pool (required, must be > 0)
  uint32_t mbuf_pool_size;
  
  // Mbuf size - data room size for packet buffers (required, must be > 0)
  // Common values: 2048 (standard Ethernet), 9216 (jumbo frames)
  // Should be set to maximum expected packet size
  uint16_t mbuf_size;
};

struct DpdkConfig {
  std::optional<std::string> core_mask;
  std::optional<int> memory_channels;
  std::vector<std::string> pci_allowlist;
  std::vector<std::string> pci_blocklist;
  std::optional<int> log_level;
  std::optional<int> huge_pages;
  
  // Port configurations
  std::vector<DpdkPortConfig> ports;
  
  // Additional EAL parameters as key-value pairs
  std::vector<std::pair<std::string, std::string>> additional_params;
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_CONFIG_H_
```

### Config Parser Interface

```cpp
// config/config_parser.h
#ifndef CONFIG_CONFIG_PARSER_H_
#define CONFIG_CONFIG_PARSER_H_

#include <string>
#include "absl/status/statusor.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

class ConfigParser {
 public:
  // Parse JSON configuration file at the given path
  // Returns DpdkConfig on success, or error status on failure
  static absl::StatusOr<DpdkConfig> ParseFile(const std::string& file_path);
  
  // Parse JSON configuration from string
  static absl::StatusOr<DpdkConfig> ParseString(const std::string& json_content);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_PARSER_H_
```

### Config Validator Interface

```cpp
// config/config_validator.h
#ifndef CONFIG_CONFIG_VALIDATOR_H_
#define CONFIG_CONFIG_VALIDATOR_H_

#include "absl/status/status.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

class ConfigValidator {
 public:
  // Validate configuration data
  // Returns OK status if valid, error status with details if invalid
  static absl::Status Validate(const DpdkConfig& config);
  
 private:
  static bool IsValidHexString(const std::string& hex);
  static bool IsValidPciAddress(const std::string& pci_addr);
  static bool IsValidLogLevel(int level);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_VALIDATOR_H_
```

### Config Printer Interface

```cpp
// config/config_printer.h
#ifndef CONFIG_CONFIG_PRINTER_H_
#define CONFIG_CONFIG_PRINTER_H_

#include <string>
#include "config/dpdk_config.h"

namespace dpdk_config {

class ConfigPrinter {
 public:
  // Format configuration as JSON string with indentation
  static std::string ToJson(const DpdkConfig& config, int indent = 2);
};

}  // namespace dpdk_config

#endif  // CONFIG_CONFIG_PRINTER_H_
```

### DPDK Initializer Interface

```cpp
// config/dpdk_initializer.h
#ifndef CONFIG_DPDK_INITIALIZER_H_
#define CONFIG_DPDK_INITIALIZER_H_

#include <vector>
#include <string>
#include "absl/status/status.h"
#include "config/dpdk_config.h"

namespace dpdk_config {

class DpdkInitializer {
 public:
  // Initialize DPDK with the given configuration
  // Returns OK status on success, error status on failure
  static absl::Status Initialize(const DpdkConfig& config, 
                                   const std::string& program_name,
                                   bool verbose = false);
  
  // Construct argv array from configuration (for testing/debugging)
  static std::vector<std::string> BuildArguments(const DpdkConfig& config,
                                                  const std::string& program_name);
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_INITIALIZER_H_
```

## Data Models

### JSON Schema

The configuration file follows this JSON schema:

```json
{
  "core_mask": "0xff",           // Hexadecimal string (optional)
  "memory_channels": 4,          // Positive integer (optional)
  "pci_allowlist": [             // Array of PCI addresses (optional)
    "0000:01:00.0",
    "0000:01:00.1"
  ],
  "pci_blocklist": [             // Array of PCI addresses (optional)
    "0000:02:00.0"
  ],
  "log_level": 7,                // Integer 0-8 (optional)
  "huge_pages": 1024,            // Positive integer (optional)
  "ports": [                     // Array of port configurations (optional)
    {
      "port_id": 0,              // Port identifier (required in port object)
      "num_rx_queues": 4,        // Number of RX queues (required, > 0)
      "num_tx_queues": 4,        // Number of TX queues (required, > 0)
      "num_descriptors": 1024,   // Descriptors per queue (required, power of 2)
      "mbuf_pool_size": 16384,   // Total mbufs in pool (required, > 0, includes per-core caches)
      "mbuf_size": 2048          // Mbuf data room size (required, > 0)
    }
  ]
}
```

### Port Configuration Schema Details

Each port configuration object in the "ports" array must contain:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| port_id | integer | Yes | Unique port identifier (0-65535) |
| num_rx_queues | integer | Yes | Number of receive queues (> 0) |
| num_tx_queues | integer | Yes | Number of transmit queues (> 0) |
| num_descriptors | integer | Yes | Descriptors per queue (power of 2, typically 128-4096) |
| mbuf_pool_size | integer | Yes | Total mbufs in memory pool (> 0) |
| mbuf_size | integer | Yes | Mbuf data room size (> 0, common: 2048 or 9216) |

### Validation Rules

| Field | Type | Validation Rule |
|-------|------|----------------|
| core_mask | string | Must be valid hexadecimal (0-9, a-f, A-F), may have "0x" prefix |
| memory_channels | integer | Must be positive (> 0) |
| pci_allowlist | array of strings | Each element must match PCI format: DDDD:BB:DD.F |
| pci_blocklist | array of strings | Each element must match PCI format: DDDD:BB:DD.F |
| log_level | integer | Must be in range [0, 8] |
| huge_pages | integer | Must be positive (> 0) |

#### Port Configuration Validation Rules

| Field | Validation Rule |
|-------|----------------|
| port_id | Must be in range [0, 65535], must be unique across all ports |
| num_rx_queues | Must be positive (> 0), typically ≤ number of CPU cores |
| num_tx_queues | Must be positive (> 0), typically ≤ number of CPU cores |
| num_descriptors | Must be power of 2, typically in range [128, 4096] |
| mbuf_pool_size | Must be positive (> 0), recommended: ≥ num_descriptors × (num_rx_queues + num_tx_queues) + 512 (accounts for per-core caches) |
| mbuf_size | Must be positive (> 0), common values: 2048 (standard Ethernet), 9216 (jumbo frames) |

Additional validation:
- A PCI address cannot appear in both allowlist and blocklist
- Empty configuration file is invalid
- Unknown fields are ignored (forward compatibility)
- Port IDs must be unique (no duplicate port_id values)

## DPDK Port Class Design

### Overview

The dpdk_port class encapsulates DPDK port configuration and initialization logic. It provides a clean interface for configuring network ports with RX/TX queues, memory pools, and packet buffer sizes.

### Port Initialization Flow

```
┌─────────────────────────┐
│  DpdkPortConfig (JSON)  │
│  - port_id              │
│  - num_rx_queues        │
│  - num_tx_queues        │
│  - num_descriptors      │
│  - mbuf_pool_size       │
│  - mbuf_size            │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│   Port Validation       │
│   - Check port exists   │
│   - Validate parameters │
│   - Check capabilities  │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│   Create Mbuf Pool      │
│   - Allocate memory     │
│   - Configure sizes     │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│   Configure Port        │
│   - Set RX/TX queues    │
│   - Set descriptors     │
│   - Apply port config   │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│   Setup RX/TX Queues    │
│   - Allocate queues     │
│   - Assign mbuf pools   │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│   Start Port            │
│   - Enable port         │
│   - Verify link status  │
└─────────────────────────┘
```

### Port Class Interface

```cpp
// config/dpdk_port.h
#ifndef CONFIG_DPDK_PORT_H_
#define CONFIG_DPDK_PORT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/dpdk_config.h"

// Forward declare DPDK types to avoid including DPDK headers
struct rte_mempool;
struct rte_eth_conf;

namespace dpdk_config {

class DpdkPort {
 public:
  // Create a port from configuration
  // Does not initialize the port - call Initialize() separately
  explicit DpdkPort(const DpdkPortConfig& config);
  
  // Destructor - cleans up resources
  ~DpdkPort();
  
  // Initialize the port with DPDK
  // Must be called after rte_eal_init()
  // Returns OK on success, error status on failure
  absl::Status Initialize();
  
  // Start the port (enable packet processing)
  // Must be called after Initialize()
  absl::Status Start();
  
  // Stop the port (disable packet processing)
  absl::Status Stop();
  
  // Get port ID
  uint16_t GetPortId() const { return config_.port_id; }
  
  // Get port statistics
  struct PortStats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
  };
  absl::StatusOr<PortStats> GetStats() const;
  
  // Check if port is initialized
  bool IsInitialized() const { return initialized_; }
  
  // Check if port is started
  bool IsStarted() const { return started_; }
  
 private:
  // Create mbuf memory pool for this port
  absl::Status CreateMbufPool();
  
  // Configure port with RX/TX parameters
  absl::Status ConfigurePort();
  
  // Setup RX queues
  absl::Status SetupRxQueues();
  
  // Setup TX queues
  absl::Status SetupTxQueues();
  
  // Validate port configuration against device capabilities
  absl::Status ValidatePortCapabilities();
  
  // Check if a number is a power of 2
  static bool IsPowerOfTwo(uint16_t n);
  
  DpdkPortConfig config_;
  rte_mempool* mbuf_pool_;
  bool initialized_;
  bool started_;
};

}  // namespace dpdk_config

#endif  // CONFIG_DPDK_PORT_H_
```

### Port Configuration Details

#### Mbuf Pool Creation

The mbuf (message buffer) pool is a pre-allocated memory pool for packet buffers:

```cpp
absl::Status DpdkPort::CreateMbufPool() {
  // Pool name must be unique per port
  std::string pool_name = absl::StrCat("mbuf_pool_", config_.port_id);
  
  // Per-core cache size for performance
  // Standard cache size is 256 mbufs per lcore
  // This reduces contention on the mempool by giving each core its own cache
  const unsigned cache_size = 256;
  
  // Use configured mbuf size
  // Add headroom for packet metadata (typically 128 bytes)
  const uint16_t mbuf_data_room = config_.mbuf_size + RTE_PKTMBUF_HEADROOM;
  
  // Create the memory pool
  // rte_pktmbuf_pool_create() automatically handles per-core caching
  // The pool size must be large enough to accommodate:
  //   1. All descriptors: num_descriptors × (num_rx_queues + num_tx_queues)
  //   2. Per-core caches: cache_size × num_cores
  //   3. Additional headroom for in-flight packets
  mbuf_pool_ = rte_pktmbuf_pool_create(
      pool_name.c_str(),
      config_.mbuf_pool_size,  // Total number of mbufs (must include cache space)
      cache_size,               // Per-core cache size (256 is standard)
      0,                        // Private data size
      mbuf_data_room,          // Data room size
      rte_socket_id()          // NUMA socket
  );
  
  if (mbuf_pool_ == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to create mbuf pool for port ", 
                     config_.port_id, ": ", rte_strerror(rte_errno)));
  }
  
  return absl::OkStatus();
}
```

#### Understanding Per-Core Mbuf Caching

DPDK mempools implement per-core caching to optimize performance:

- **Per-core cache**: Each logical core (lcore) gets its own private cache of mbufs
- **Cache size**: Typically 256 mbufs per core (standard for good performance)
- **Reduced contention**: Cores allocate/free mbufs from their local cache, avoiding atomic operations on the shared pool
- **Automatic management**: `rte_pktmbuf_pool_create()` automatically sets up per-core caching

**Memory calculation:**
```
Total memory = (num_descriptors × queues) + (cache_size × num_cores) + headroom
```

For example, with 8 cores and cache_size=256:
- Per-core cache memory: 256 × 8 = 2,048 mbufs
- This is in addition to the descriptor requirements
- The mbuf_pool_size must be large enough to cover both

**Important**: If the pool size is too small to accommodate all per-core caches plus descriptors, port initialization will fail with an "insufficient memory" error.

#### Port Configuration Structure

DPDK ports are configured using `rte_eth_conf`:

```cpp
absl::Status DpdkPort::ConfigurePort() {
  // Get device info to check capabilities
  struct rte_eth_dev_info dev_info;
  int ret = rte_eth_dev_info_get(config_.port_id, &dev_info);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to get device info for port ", 
                     config_.port_id));
  }
  
  // Validate queue counts against device limits
  if (config_.num_rx_queues > dev_info.max_rx_queues) {
    return absl::InvalidArgumentError(
        absl::StrCat("RX queue count ", config_.num_rx_queues,
                     " exceeds device maximum ", dev_info.max_rx_queues));
  }
  
  if (config_.num_tx_queues > dev_info.max_tx_queues) {
    return absl::InvalidArgumentError(
        absl::StrCat("TX queue count ", config_.num_tx_queues,
                     " exceeds device maximum ", dev_info.max_tx_queues));
  }
  
  // Configure port with default settings
  struct rte_eth_conf port_conf = {};
  port_conf.rxmode.max_rx_pkt_len = RTE_ETHER_MAX_LEN;
  
  // Enable jumbo frames if mbuf size exceeds standard Ethernet
  if (config_.mbuf_size > RTE_ETHER_MAX_LEN) {
    port_conf.rxmode.offloads |= DEV_RX_OFFLOAD_JUMBO_FRAME;
    port_conf.rxmode.max_rx_pkt_len = config_.mbuf_size;
  }
  
  // Configure the port
  ret = rte_eth_dev_configure(
      config_.port_id,
      config_.num_rx_queues,
      config_.num_tx_queues,
      &port_conf);
  
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to configure port ", config_.port_id,
                     ": ", rte_strerror(-ret)));
  }
  
  return absl::OkStatus();
}
```

#### RX Queue Setup

```cpp
absl::Status DpdkPort::SetupRxQueues() {
  for (uint16_t queue_id = 0; queue_id < config_.num_rx_queues; ++queue_id) {
    int ret = rte_eth_rx_queue_setup(
        config_.port_id,
        queue_id,
        config_.num_descriptors,  // Number of descriptors
        rte_eth_dev_socket_id(config_.port_id),  // NUMA socket
        nullptr,                  // Use default RX config
        mbuf_pool_               // Mbuf pool for this queue
    );
    
    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("Failed to setup RX queue ", queue_id,
                       " on port ", config_.port_id,
                       ": ", rte_strerror(-ret)));
    }
  }
  
  return absl::OkStatus();
}
```

#### TX Queue Setup

```cpp
absl::Status DpdkPort::SetupTxQueues() {
  for (uint16_t queue_id = 0; queue_id < config_.num_tx_queues; ++queue_id) {
    int ret = rte_eth_tx_queue_setup(
        config_.port_id,
        queue_id,
        config_.num_descriptors,  // Number of descriptors
        rte_eth_dev_socket_id(config_.port_id),  // NUMA socket
        nullptr                   // Use default TX config
    );
    
    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("Failed to setup TX queue ", queue_id,
                       " on port ", config_.port_id,
                       ": ", rte_strerror(-ret)));
    }
  }
  
  return absl::OkStatus();
}
```

### Port Manager Class

To manage multiple ports, we introduce a PortManager class:

```cpp
// config/port_manager.h
#ifndef CONFIG_PORT_MANAGER_H_
#define CONFIG_PORT_MANAGER_H_

#include <memory>
#include <unordered_map>
#include <vector>
#include "absl/status/status.h"
#include "config/dpdk_config.h"
#include "config/dpdk_port.h"

namespace dpdk_config {

class PortManager {
 public:
  PortManager() = default;
  
  // Initialize all ports from configuration
  // Must be called after rte_eal_init()
  absl::Status InitializePorts(const std::vector<DpdkPortConfig>& port_configs);
  
  // Start all initialized ports
  absl::Status StartAllPorts();
  
  // Stop all running ports
  absl::Status StopAllPorts();
  
  // Get a specific port by ID
  DpdkPort* GetPort(uint16_t port_id);
  
  // Get all port IDs
  std::vector<uint16_t> GetPortIds() const;
  
  // Get number of initialized ports
  size_t GetPortCount() const { return ports_.size(); }
  
 private:
  std::unordered_map<uint16_t, std::unique_ptr<DpdkPort>> ports_;
};

}  // namespace dpdk_config

#endif  // CONFIG_PORT_MANAGER_H_
```

### Integration with Existing Components

#### Config Parser Updates

The ConfigParser needs to parse the "ports" array from JSON:

```cpp
// In config_parser.cc
absl::StatusOr<DpdkConfig> ConfigParser::ParseString(const std::string& json_content) {
  // ... existing parsing code ...
  
  // Parse ports array
  if (j.contains("ports") && j["ports"].is_array()) {
    for (const auto& port_json : j["ports"]) {
      DpdkPortConfig port_config;
      
      // Required fields
      if (!port_json.contains("port_id")) {
        return absl::InvalidArgumentError("Port configuration missing required field: port_id");
      }
      port_config.port_id = port_json["port_id"].get<uint16_t>();
      
      if (!port_json.contains("num_rx_queues")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, " missing required field: num_rx_queues"));
      }
      port_config.num_rx_queues = port_json["num_rx_queues"].get<uint16_t>();
      
      if (!port_json.contains("num_tx_queues")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, " missing required field: num_tx_queues"));
      }
      port_config.num_tx_queues = port_json["num_tx_queues"].get<uint16_t>();
      
      if (!port_json.contains("num_descriptors")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, " missing required field: num_descriptors"));
      }
      port_config.num_descriptors = port_json["num_descriptors"].get<uint16_t>();
      
      if (!port_json.contains("mbuf_pool_size")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, " missing required field: mbuf_pool_size"));
      }
      port_config.mbuf_pool_size = port_json["mbuf_pool_size"].get<uint32_t>();
      
      if (!port_json.contains("mbuf_size")) {
        return absl::InvalidArgumentError(
            absl::StrCat("Port ", port_config.port_id, " missing required field: mbuf_size"));
      }
      port_config.mbuf_size = port_json["mbuf_size"].get<uint16_t>();
      
      config.ports.push_back(port_config);
    }
  }
  
  return config;
}
```

#### Config Validator Updates

The ConfigValidator needs to validate port configurations:

```cpp
// In config_validator.cc
absl::Status ConfigValidator::Validate(const DpdkConfig& config) {
  // ... existing validation code ...
  
  // Validate port configurations
  std::unordered_set<uint16_t> seen_port_ids;
  
  for (const auto& port : config.ports) {
    // Check for duplicate port IDs
    if (seen_port_ids.count(port.port_id) > 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate port_id: ", port.port_id));
    }
    seen_port_ids.insert(port.port_id);
    
    // Validate num_rx_queues
    if (port.num_rx_queues == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": num_rx_queues must be > 0"));
    }
    
    // Validate num_tx_queues
    if (port.num_tx_queues == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": num_tx_queues must be > 0"));
    }
    
    // Validate num_descriptors is power of 2
    if (!IsPowerOfTwo(port.num_descriptors)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, 
                       ": num_descriptors must be a power of 2"));
    }
    
    // Validate mbuf_pool_size
    if (port.mbuf_pool_size == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": mbuf_pool_size must be > 0"));
    }
    
    // Recommend minimum pool size accounting for per-core caches
    // Formula: descriptors × queues + cache headroom (512 = ~2 cores × 256 cache)
    uint32_t min_recommended = port.num_descriptors * 
                               (port.num_rx_queues + port.num_tx_queues) + 512;
    if (port.mbuf_pool_size < min_recommended) {
      // This is a warning, not an error - log but don't fail validation
      // In production code, this could use a logging framework
      std::cerr << "Warning: Port " << port.port_id 
                << " mbuf_pool_size (" << port.mbuf_pool_size 
                << ") is below recommended minimum (" << min_recommended
                << "). Consider increasing to account for per-core caches.\n";
    }
    
    // Validate mbuf_size
    if (port.mbuf_size == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Port ", port.port_id, ": mbuf_size must be > 0"));
    }
  }
  
  return absl::OkStatus();
}
```

#### Config Printer Updates

The ConfigPrinter needs to serialize port configurations:

```cpp
// In config_printer.cc
std::string ConfigPrinter::ToJson(const DpdkConfig& config, int indent) {
  json j;
  
  // ... existing serialization code ...
  
  // Serialize ports
  if (!config.ports.empty()) {
    json ports_array = json::array();
    
    for (const auto& port : config.ports) {
      json port_json;
      port_json["port_id"] = port.port_id;
      port_json["num_rx_queues"] = port.num_rx_queues;
      port_json["num_tx_queues"] = port.num_tx_queues;
      port_json["num_descriptors"] = port.num_descriptors;
      port_json["mbuf_pool_size"] = port.mbuf_pool_size;
      port_json["mbuf_size"] = port.mbuf_size;
      
      ports_array.push_back(port_json);
    }
    
    j["ports"] = ports_array;
  }
  
  return j.dump(indent);
}
```

#### DPDK Initializer Updates

The DpdkInitializer needs to initialize ports after EAL initialization:

```cpp
// In dpdk_initializer.cc
absl::Status DpdkInitializer::Initialize(
    const DpdkConfig& config, 
    const std::string& program_name,
    bool verbose) {
  // ... existing EAL initialization code ...
  
  // Initialize ports if configured
  if (!config.ports.empty()) {
    if (verbose) {
      std::cout << "Initializing " << config.ports.size() << " port(s)...\n";
    }
    
    PortManager port_manager;
    absl::Status status = port_manager.InitializePorts(config.ports);
    if (!status.ok()) {
      return status;
    }
    
    status = port_manager.StartAllPorts();
    if (!status.ok()) {
      return status;
    }
    
    if (verbose) {
      std::cout << "All ports initialized and started successfully\n";
    }
  }
  
  return absl::OkStatus();
}
```

### Port Configuration Best Practices

#### Descriptor Count Sizing

- **Small deployments**: 128-512 descriptors
- **Medium deployments**: 1024 descriptors (most common)
- **High throughput**: 2048-4096 descriptors
- Must be power of 2 for hardware efficiency

#### Mbuf Pool Sizing

DPDK mempools use per-core caching for performance optimization. Each logical core (lcore) maintains its own cache of mbufs to reduce contention on the shared mempool. The typical cache size is 256 mbufs per core.

The total pool size must account for:
1. **Descriptors**: All RX and TX queue descriptors need backing mbufs
2. **Per-core caches**: Each lcore gets its own cache (typically 256 mbufs)
3. **Headroom**: Additional buffers for in-flight packet processing

**Detailed formula:**
```
mbuf_pool_size >= num_descriptors × (num_rx_queues + num_tx_queues) + (cache_size × num_cores)
```

**Simplified formula (recommended):**
```
mbuf_pool_size >= num_descriptors × (num_rx_queues + num_tx_queues) × 2
```

The factor of 2 accounts for per-core caches and provides headroom for packet processing. This simplified approach works well for most deployments where the number of cores is reasonable relative to the descriptor count.

**Example calculation:**
- 4 RX queues, 4 TX queues, 1024 descriptors per queue
- 8 CPU cores, cache size of 256 per core
- Minimum pool size = 1024 × (4 + 4) + (256 × 8) = 8192 + 2048 = 10,240 mbufs
- Using simplified formula: 1024 × 8 × 2 = 16,384 mbufs (provides extra headroom)

#### Mbuf Size Selection

Common configurations:
- **Standard Ethernet**: `2048` - handles packets up to 1518 bytes
- **Jumbo frames**: `9216` - handles packets up to 9000 bytes

The mbuf_size should be set to the maximum expected packet size for the port. DPDK creates one mbuf pool per port with a single data room size.

#### Queue Count Guidelines

- Match queue count to number of CPU cores for best performance
- Use RSS (Receive Side Scaling) to distribute traffic across RX queues
- TX queues typically match RX queues for symmetric processing

### EAL Argument Mapping

| Configuration Field | DPDK Argument | Example |
|---------------------|---------------|---------|
| core_mask | `-c <mask>` | `-c 0xff` |
| memory_channels | `-n <channels>` | `-n 4` |
| pci_allowlist[i] | `-a <addr>` | `-a 0000:01:00.0` |
| pci_blocklist[i] | `-b <addr>` | `-b 0000:02:00.0` |
| log_level | `--log-level <level>` | `--log-level 7` |

Note: huge_pages is not directly mapped to an EAL argument in this design, as DPDK typically uses hugepages configured at the system level. This field is reserved for future use or custom handling.

## JSON Library Selection

### Library Choice: nlohmann/json

We will use the nlohmann/json library for JSON parsing and serialization:

**Rationale:**
- Header-only library (easy Bazel integration)
- Modern C++ interface with intuitive API
- Excellent error reporting with line numbers
- Wide adoption in C++ community
- MIT license (permissive)
- Strong type safety with automatic conversions

**Bazel Integration:**
```python
# Add to MODULE.bazel
bazel_dep(name = "nlohmann_json", version = "3.11.3")
```

**Usage Example:**
```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Parsing
json j = json::parse(file_content);
std::string core_mask = j.value("core_mask", "");

// Serialization
json j;
j["core_mask"] = "0xff";
j["memory_channels"] = 4;
std::string output = j.dump(2);  // 2-space indentation
```

## Configuration Validation Logic

### Validation Strategy

Validation occurs in two phases:

1. **Structural Validation** (during parsing)
   - JSON syntax correctness
   - Type checking (string vs integer vs array)
   - Handled by nlohmann/json library

2. **Semantic Validation** (after parsing)
   - Value range checking
   - Format validation (hex strings, PCI addresses)
   - Cross-field validation (allowlist/blocklist conflicts)
   - Handled by ConfigValidator

### Validation Implementation

```cpp
// Pseudo-code for ConfigValidator::Validate()
absl::Status ConfigValidator::Validate(const DpdkConfig& config) {
  // Validate core_mask format
  if (config.core_mask.has_value()) {
    if (!IsValidHexString(*config.core_mask)) {
      return absl::InvalidArgumentError(
        "core_mask must be a valid hexadecimal string");
    }
  }
  
  // Validate memory_channels range
  if (config.memory_channels.has_value()) {
    if (*config.memory_channels <= 0) {
      return absl::InvalidArgumentError(
        "memory_channels must be positive");
    }
  }
  
  // Validate PCI addresses
  for (const auto& addr : config.pci_allowlist) {
    if (!IsValidPciAddress(addr)) {
      return absl::InvalidArgumentError(
        absl::StrCat("Invalid PCI address in allowlist: ", addr));
    }
  }
  
  // Check for allowlist/blocklist conflicts
  for (const auto& addr : config.pci_allowlist) {
    if (std::find(config.pci_blocklist.begin(), 
                  config.pci_blocklist.end(), addr) 
        != config.pci_blocklist.end()) {
      return absl::InvalidArgumentError(
        absl::StrCat("PCI address appears in both allowlist and blocklist: ", addr));
    }
  }
  
  // Validate log_level range
  if (config.log_level.has_value()) {
    if (!IsValidLogLevel(*config.log_level)) {
      return absl::InvalidArgumentError(
        "log_level must be between 0 and 8");
    }
  }
  
  return absl::OkStatus();
}
```

### PCI Address Validation

PCI addresses follow the format: `DDDD:BB:DD.F`
- DDDD: 4-digit hexadecimal domain
- BB: 2-digit hexadecimal bus
- DD: 2-digit hexadecimal device
- F: 1-digit hexadecimal function

Regex pattern: `^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9a-fA-F]$`

## DPDK Initialization with Constructed Arguments

### Argument Construction

The DpdkInitializer builds an argv array from the configuration:

```cpp
std::vector<std::string> DpdkInitializer::BuildArguments(
    const DpdkConfig& config, const std::string& program_name) {
  std::vector<std::string> args;
  
  // argv[0] is always the program name
  args.push_back(program_name);
  
  // Add core mask
  if (config.core_mask.has_value()) {
    args.push_back("-c");
    args.push_back(*config.core_mask);
  }
  
  // Add memory channels
  if (config.memory_channels.has_value()) {
    args.push_back("-n");
    args.push_back(std::to_string(*config.memory_channels));
  }
  
  // Add PCI allowlist
  for (const auto& addr : config.pci_allowlist) {
    args.push_back("-a");
    args.push_back(addr);
  }
  
  // Add PCI blocklist
  for (const auto& addr : config.pci_blocklist) {
    args.push_back("-b");
    args.push_back(addr);
  }
  
  // Add log level
  if (config.log_level.has_value()) {
    args.push_back("--log-level");
    args.push_back(std::to_string(*config.log_level));
  }
  
  return args;
}
```

### Initialization Process

```cpp
absl::Status DpdkInitializer::Initialize(
    const DpdkConfig& config, 
    const std::string& program_name,
    bool verbose) {
  // Build argument vector
  std::vector<std::string> args = BuildArguments(config, program_name);
  
  if (verbose) {
    std::cout << "DPDK initialization arguments: ";
    for (const auto& arg : args) {
      std::cout << arg << " ";
    }
    std::cout << "\n";
  }
  
  // Convert to C-style argc/argv
  std::vector<char*> argv;
  for (auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  int argc = argv.size();
  
  // Call rte_eal_init
  int ret = rte_eal_init(argc, argv.data());
  
  if (ret < 0) {
    return absl::InternalError(
      absl::StrCat("DPDK initialization failed: ", rte_strerror(rte_errno)));
  }
  
  if (verbose) {
    std::cout << "DPDK initialization successful\n";
  }
  
  return absl::OkStatus();
}
```

### Integration with main.cc

```cpp
// main.cc modifications
ABSL_FLAG(std::string, i, "", "Path to JSON configuration file");

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  
  std::string config_file = absl::GetFlag(FLAGS_i);
  bool verbose = absl::GetFlag(FLAGS_verbose);
  
  if (!config_file.empty()) {
    // Load configuration from file
    auto config_or = dpdk_config::ConfigParser::ParseFile(config_file);
    if (!config_or.ok()) {
      std::cerr << "Configuration error: " << config_or.status() << "\n";
      return 1;
    }
    
    // Validate configuration
    auto validation_status = dpdk_config::ConfigValidator::Validate(*config_or);
    if (!validation_status.ok()) {
      std::cerr << "Validation error: " << validation_status << "\n";
      return 1;
    }
    
    if (verbose) {
      std::cout << "Loaded configuration:\n";
      std::cout << dpdk_config::ConfigPrinter::ToJson(*config_or) << "\n";
    }
    
    // Initialize DPDK with configuration
    auto init_status = dpdk_config::DpdkInitializer::Initialize(
        *config_or, argv[0], verbose);
    if (!init_status.ok()) {
      std::cerr << "DPDK initialization error: " << init_status << "\n";
      return 1;
    }
  } else {
    // Original behavior: pass arguments directly to DPDK
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
      std::cerr << "DPDK initialization failed\n";
      return 1;
    }
  }
  
  // Continue with application logic...
  return 0;
}
```


## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Configuration Round-Trip Preservation

*For any* valid DpdkConfig structure (including port configurations), serializing it to JSON with ConfigPrinter::ToJson() and then parsing it back with ConfigParser::ParseString() should produce an equivalent configuration structure.

**Validates: Requirements 6.4**

This property ensures that the configuration can be reliably saved and loaded without data loss or corruption. It validates that the parser and printer are inverse operations.

### Property 2: Valid Hexadecimal Strings Pass Validation

*For any* string composed only of hexadecimal characters (0-9, a-f, A-F) with optional "0x" prefix, when used as a core_mask value, ConfigValidator::Validate() should return OK status.

**Validates: Requirements 4.1**

### Property 3: Invalid Hexadecimal Strings Fail Validation

*For any* string containing non-hexadecimal characters (excluding valid "0x" prefix), when used as a core_mask value, ConfigValidator::Validate() should return an error status.

**Validates: Requirements 4.1**

### Property 4: Valid PCI Addresses Pass Validation

*For any* string matching the format DDDD:BB:DD.F (where D, B, and F are hexadecimal digits), when used in pci_allowlist or pci_blocklist, ConfigValidator::Validate() should return OK status.

**Validates: Requirements 4.3**

### Property 5: PCI Address Conflict Detection

*For any* PCI address string, if it appears in both pci_allowlist and pci_blocklist, ConfigValidator::Validate() should return an error status indicating the conflict.

**Validates: Requirements 4.7**

### Property 6: Port ID Uniqueness Validation

*For any* configuration with multiple ports, if two or more ports have the same port_id value, ConfigValidator::Validate() should return an error status indicating duplicate port IDs.

**Validates: Port configuration validation requirements**

### Property 7: Power of Two Descriptor Validation

*For any* port configuration, if num_descriptors is not a power of 2, ConfigValidator::Validate() should return an error status.

**Validates: Port configuration validation requirements**

This property ensures hardware compatibility, as DPDK network devices require descriptor counts to be powers of 2 for efficient ring buffer operations.

### Property 8: Positive Queue Count Validation

*For any* port configuration, if num_rx_queues or num_tx_queues is zero or negative, ConfigValidator::Validate() should return an error status.

**Validates: Port configuration validation requirements**

### Property 9: Positive Mbuf Size Validation

*For any* port configuration, if mbuf_size is zero or negative, ConfigValidator::Validate() should return an error status.

**Validates: Port configuration validation requirements**

This property ensures that each port has a valid mbuf data room size configured.

### Property 10: Port Configuration Parsing Completeness

*For any* valid JSON port configuration object containing all required fields (port_id, num_rx_queues, num_tx_queues, num_descriptors, mbuf_pool_size, mbuf_size), ConfigParser::ParseString() should successfully parse it into a DpdkPortConfig structure with all fields correctly populated.

**Validates: Requirements 2.2, 2.6**

## Error Handling

### Error Handling Strategy

The system uses Abseil Status for error propagation and reporting. All errors are categorized and include contextual information to aid debugging.

### Error Categories

| Error Type | Status Code | When It Occurs | Example Message |
|------------|-------------|----------------|-----------------|
| File Not Found | NotFound | Config file doesn't exist | "Configuration file not found: /path/to/config.json" |
| JSON Parse Error | InvalidArgument | Invalid JSON syntax | "JSON parse error at line 5: unexpected token" |
| Validation Error | InvalidArgument | Invalid configuration values | "core_mask must be a valid hexadecimal string" |
| DPDK Init Error | Internal | DPDK initialization fails | "DPDK initialization failed: Cannot init memory" |
| Port Init Error | Internal | Port initialization fails | "Failed to configure port 0: Invalid argument" |
| Missing Field Error | InvalidArgument | Required field missing | "Port 0 missing required field: num_rx_queues" |

### Error Propagation

```cpp
// Example error propagation pattern
absl::Status DpdkInitializer::Initialize(const DpdkConfig& config, ...) {
  // Validate first
  absl::Status validation = ConfigValidator::Validate(config);
  if (!validation.ok()) {
    return validation;  // Propagate validation error
  }
  
  // Initialize EAL
  int ret = rte_eal_init(argc, argv.data());
  if (ret < 0) {
    return absl::InternalError(
        absl::StrCat("DPDK initialization failed: ", 
                     rte_strerror(rte_errno)));
  }
  
  // Initialize ports
  if (!config.ports.empty()) {
    PortManager port_manager;
    absl::Status port_status = port_manager.InitializePorts(config.ports);
    if (!port_status.ok()) {
      return absl::InternalError(
          absl::StrCat("Port initialization failed: ", 
                       port_status.message()));
    }
  }
  
  return absl::OkStatus();
}
```

### Error Reporting to User

All errors are reported to stderr with clear, actionable messages:

```cpp
// In main.cc
auto config_or = dpdk_config::ConfigParser::ParseFile(config_file);
if (!config_or.ok()) {
  std::cerr << "Configuration error: " << config_or.status() << "\n";
  return 1;
}
```

### Port-Specific Error Handling

Port initialization errors include:
- Port ID out of range or doesn't exist
- Queue count exceeds device capabilities
- Descriptor count not power of 2
- Mbuf pool creation failure (insufficient memory)
- Queue setup failure (invalid parameters)

Each error includes the port ID and specific failure reason:

```cpp
return absl::InternalError(
    absl::StrCat("Port ", port_id, ": Failed to setup RX queue ", 
                 queue_id, ": ", rte_strerror(-ret)));
```

## Testing Strategy

### Dual Testing Approach

The implementation uses both unit tests and property-based tests for comprehensive coverage:

- **Unit tests**: Verify specific examples, edge cases, and error conditions
- **Property tests**: Verify universal properties across all inputs

Both testing approaches are complementary and necessary. Unit tests catch concrete bugs in specific scenarios, while property tests verify general correctness across a wide range of inputs.

### Property-Based Testing

We use the **Google Test** framework with custom property test helpers for C++. Each property test runs a minimum of 100 iterations with randomized inputs.

#### Property Test Configuration

```cpp
// Example property test structure
TEST(ConfigRoundTripTest, PreservesAllFields) {
  // Feature: dpdk-json-config, Property 1: Configuration Round-Trip Preservation
  
  for (int i = 0; i < 100; ++i) {
    // Generate random valid configuration
    DpdkConfig original = GenerateRandomConfig();
    
    // Serialize to JSON
    std::string json = ConfigPrinter::ToJson(original);
    
    // Parse back
    auto parsed = ConfigParser::ParseString(json);
    ASSERT_TRUE(parsed.ok());
    
    // Verify equivalence
    EXPECT_EQ(original, *parsed);
  }
}
```

#### Property Test Tags

Each property test includes a comment tag referencing the design property:

```cpp
// Feature: dpdk-json-config, Property 1: Configuration Round-Trip Preservation
// Feature: dpdk-json-config, Property 6: Port ID Uniqueness Validation
```

### Unit Testing Strategy

#### Config Parser Tests

- Valid JSON parsing (various field combinations)
- Invalid JSON syntax handling
- Missing file handling
- Empty file handling
- Port configuration parsing (all required fields)
- Port configuration parsing (missing required fields)

#### Config Validator Tests

- Valid core_mask formats ("0xff", "ff", "0xFF")
- Invalid core_mask formats ("xyz", "0xgg")
- Valid PCI addresses ("0000:01:00.0")
- Invalid PCI addresses ("invalid", "00:00:00.0")
- PCI allowlist/blocklist conflicts
- Log level range validation (0-8)
- Port ID uniqueness
- Descriptor power-of-2 validation
- Queue count validation
- Mbuf size validation (must be > 0)

#### Config Printer Tests

- Serialization of all field types
- Proper JSON formatting and indentation
- Handling of optional fields (present vs absent)
- Port configuration serialization

#### DPDK Initializer Tests

- Argument construction from configuration
- Correct mapping of config fields to DPDK arguments
- Port initialization sequence
- Error handling for DPDK failures

### Integration Testing

Integration tests verify the complete flow:

1. Load configuration from file
2. Validate configuration
3. Initialize DPDK EAL
4. Initialize ports
5. Verify ports are operational

### Test Data Generators

For property-based testing, we implement generators for random valid configurations:

```cpp
// Generate random valid DpdkConfig
DpdkConfig GenerateRandomConfig() {
  DpdkConfig config;
  
  // Randomly include optional fields
  if (RandomBool()) {
    config.core_mask = GenerateRandomHexString();
  }
  
  if (RandomBool()) {
    config.memory_channels = RandomInt(1, 8);
  }
  
  // Generate random port configurations
  int num_ports = RandomInt(0, 4);
  for (int i = 0; i < num_ports; ++i) {
    config.ports.push_back(GenerateRandomPortConfig(i));
  }
  
  return config;
}

DpdkPortConfig GenerateRandomPortConfig(uint16_t port_id) {
  DpdkPortConfig port;
  port.port_id = port_id;
  port.num_rx_queues = RandomInt(1, 16);
  port.num_tx_queues = RandomInt(1, 16);
  port.num_descriptors = RandomPowerOfTwo(128, 4096);
  
  // Calculate realistic mbuf_pool_size accounting for per-core caches
  // Use formula: descriptors × (rx_queues + tx_queues) × 2
  // The factor of 2 accounts for per-core caches and headroom
  uint32_t min_pool_size = port.num_descriptors * 
                           (port.num_rx_queues + port.num_tx_queues) * 2;
  // Add some randomness above the minimum
  port.mbuf_pool_size = min_pool_size + RandomInt(0, 4096);
  
  // Generate mbuf size (common values: 2048 or 9216)
  port.mbuf_size = RandomChoice({2048, 9216});
  
  return port;
}
```

### Test Coverage Goals

- **Line coverage**: > 90%
- **Branch coverage**: > 85%
- **Property tests**: Minimum 100 iterations per property
- **Unit tests**: Cover all error paths and edge cases

### Continuous Testing

Tests run automatically on:
- Every commit (pre-commit hook)
- Pull requests (CI pipeline)
- Nightly builds (extended property test iterations: 1000+)
