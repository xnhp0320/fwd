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
