// config/port_manager.cc
#include "config/port_manager.h"

#include <memory>
#include "absl/strings/str_cat.h"

namespace dpdk_config {

absl::Status PortManager::InitializePorts(
    const std::vector<DpdkPortConfig>& port_configs) {
  // Clear any existing ports
  ports_.clear();
  
  // Create and initialize each port
  for (const auto& config : port_configs) {
    // Create new DpdkPort instance
    auto port = std::make_unique<DpdkPort>(config);
    
    // Initialize the port
    absl::Status status = port->Initialize();
    if (!status.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to initialize port ", config.port_id, ": ",
                       status.message()));
    }
    
    // Store the port in the map
    ports_[config.port_id] = std::move(port);
  }
  
  return absl::OkStatus();
}

absl::Status PortManager::StartAllPorts() {
  // Start each port
  for (auto& [port_id, port] : ports_) {
    absl::Status status = port->Start();
    if (!status.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to start port ", port_id, ": ",
                       status.message()));
    }
  }
  
  return absl::OkStatus();
}

absl::Status PortManager::StopAllPorts() {
  // Stop each port
  for (auto& [port_id, port] : ports_) {
    absl::Status status = port->Stop();
    if (!status.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to stop port ", port_id, ": ",
                       status.message()));
    }
  }
  
  return absl::OkStatus();
}

DpdkPort* PortManager::GetPort(uint16_t port_id) {
  auto it = ports_.find(port_id);
  if (it == ports_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<uint16_t> PortManager::GetPortIds() const {
  std::vector<uint16_t> port_ids;
  port_ids.reserve(ports_.size());
  
  for (const auto& [port_id, port] : ports_) {
    port_ids.push_back(port_id);
  }
  
  return port_ids;
}

}  // namespace dpdk_config
