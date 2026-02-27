// config/dpdk_port.cc
#include "config/dpdk_port.h"

#include <iostream>
#include <rte_ethdev.h>
#include <rte_errno.h>
#include <rte_mbuf.h>

#include "absl/strings/str_cat.h"

namespace dpdk_config {

DpdkPort::DpdkPort(const DpdkPortConfig& config)
    : config_(config), mbuf_pool_(nullptr), initialized_(false), started_(false) {}

DpdkPort::~DpdkPort() {
  if (started_) {
    // Ignore errors during cleanup
    (void)Stop();
  }
  // Note: mbuf_pool_ is managed by DPDK and should not be freed manually
}

absl::Status DpdkPort::Initialize() {
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Port ", config_.port_id, " is already initialized"));
  }

  // Validate port capabilities
  absl::Status status = ValidatePortCapabilities();
  if (!status.ok()) {
    return status;
  }

  // Create mbuf pool
  status = CreateMbufPool();
  if (!status.ok()) {
    return status;
  }

  // Configure port
  status = ConfigurePort();
  if (!status.ok()) {
    return status;
  }

  // Setup RX queues
  status = SetupRxQueues();
  if (!status.ok()) {
    return status;
  }

  // Setup TX queues
  status = SetupTxQueues();
  if (!status.ok()) {
    return status;
  }

  initialized_ = true;
  return absl::OkStatus();
}

absl::Status DpdkPort::Start() {
  if (!initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Port ", config_.port_id, " is not initialized"));
  }

  if (started_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Port ", config_.port_id, " is already started"));
  }

  int ret = rte_eth_dev_start(config_.port_id);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to start port ", config_.port_id, ": ",
                     rte_strerror(-ret)));
  }

  started_ = true;
  return absl::OkStatus();
}

absl::Status DpdkPort::Stop() {
  if (!started_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Port ", config_.port_id, " is not started"));
  }

  int ret = rte_eth_dev_stop(config_.port_id);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to stop port ", config_.port_id, ": ",
                     rte_strerror(-ret)));
  }

  started_ = false;
  return absl::OkStatus();
}

absl::StatusOr<DpdkPort::PortStats> DpdkPort::GetStats() const {
  if (!initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Port ", config_.port_id, " is not initialized"));
  }

  struct rte_eth_stats stats;
  int ret = rte_eth_stats_get(config_.port_id, &stats);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to get stats for port ", config_.port_id, ": ",
                     rte_strerror(-ret)));
  }

  PortStats port_stats;
  port_stats.rx_packets = stats.ipackets;
  port_stats.tx_packets = stats.opackets;
  port_stats.rx_bytes = stats.ibytes;
  port_stats.tx_bytes = stats.obytes;
  port_stats.rx_errors = stats.ierrors;
  port_stats.tx_errors = stats.oerrors;

  return port_stats;
}

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
        absl::StrCat("Failed to create mbuf pool for port ", config_.port_id,
                     ": ", rte_strerror(rte_errno)));
  }

  return absl::OkStatus();
}

absl::Status DpdkPort::ConfigurePort() {
  // Get device info to check capabilities
  struct rte_eth_dev_info dev_info;
  int ret = rte_eth_dev_info_get(config_.port_id, &dev_info);
  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to get device info for port ", config_.port_id,
                     ": ", rte_strerror(-ret)));
  }

  // Validate queue counts against device limits
  if (config_.num_rx_queues > dev_info.max_rx_queues) {
    return absl::InvalidArgumentError(
        absl::StrCat("RX queue count ", config_.num_rx_queues,
                     " exceeds device maximum ", dev_info.max_rx_queues,
                     " for port ", config_.port_id));
  }

  if (config_.num_tx_queues > dev_info.max_tx_queues) {
    return absl::InvalidArgumentError(
        absl::StrCat("TX queue count ", config_.num_tx_queues,
                     " exceeds device maximum ", dev_info.max_tx_queues,
                     " for port ", config_.port_id));
  }

  // Configure port with default settings
  struct rte_eth_conf port_conf = {};
  
  // Set maximum receive packet length
  // In newer DPDK versions, this is set via mtu_set instead of max_rx_pkt_len
  // For now, we'll configure basic settings and let DPDK use defaults
  
  // Enable jumbo frames if mbuf size exceeds standard Ethernet
  if (config_.mbuf_size > RTE_ETHER_MAX_LEN) {
    // Jumbo frame support is now handled via MTU configuration
    // We'll set this after port configuration
    port_conf.rxmode.mtu = config_.mbuf_size - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN;
  }

  // Configure the port
  ret = rte_eth_dev_configure(config_.port_id, config_.num_rx_queues,
                               config_.num_tx_queues, &port_conf);

  if (ret != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to configure port ", config_.port_id, ": ",
                     rte_strerror(-ret)));
  }

  return absl::OkStatus();
}

absl::Status DpdkPort::SetupRxQueues() {
  for (uint16_t queue_id = 0; queue_id < config_.num_rx_queues; ++queue_id) {
    int ret = rte_eth_rx_queue_setup(
        config_.port_id, queue_id,
        config_.num_descriptors,                 // Number of descriptors
        rte_eth_dev_socket_id(config_.port_id),  // NUMA socket
        nullptr,                                  // Use default RX config
        mbuf_pool_                                // Mbuf pool for this queue
    );

    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("Failed to setup RX queue ", queue_id, " on port ",
                       config_.port_id, ": ", rte_strerror(-ret)));
    }
  }

  return absl::OkStatus();
}

absl::Status DpdkPort::SetupTxQueues() {
  for (uint16_t queue_id = 0; queue_id < config_.num_tx_queues; ++queue_id) {
    int ret = rte_eth_tx_queue_setup(
        config_.port_id, queue_id,
        config_.num_descriptors,                 // Number of descriptors
        rte_eth_dev_socket_id(config_.port_id),  // NUMA socket
        nullptr                                   // Use default TX config
    );

    if (ret != 0) {
      return absl::InternalError(
          absl::StrCat("Failed to setup TX queue ", queue_id, " on port ",
                       config_.port_id, ": ", rte_strerror(-ret)));
    }
  }

  return absl::OkStatus();
}

absl::Status DpdkPort::ValidatePortCapabilities() {
  // Check if port exists
  if (!rte_eth_dev_is_valid_port(config_.port_id)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Port ", config_.port_id, " is not a valid port"));
  }

  // Validate num_descriptors is power of 2
  if (!IsPowerOfTwo(config_.num_descriptors)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Port ", config_.port_id,
                     ": num_descriptors must be a power of 2, got ",
                     config_.num_descriptors));
  }

  return absl::OkStatus();
}

bool DpdkPort::IsPowerOfTwo(uint16_t n) {
  // A number is a power of 2 if it has exactly one bit set
  // n & (n-1) clears the lowest set bit
  // If n is a power of 2, this results in 0
  return n > 0 && (n & (n - 1)) == 0;
}

}  // namespace dpdk_config
