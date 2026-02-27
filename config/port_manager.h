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
