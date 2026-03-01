#ifndef PROCESSOR_PACKET_PROCESSOR_BASE_H_
#define PROCESSOR_PACKET_PROCESSOR_BASE_H_

#include "absl/status/status.h"
#include "config/dpdk_config.h"

namespace processor {

// CRTP base class for packet processors.
// Derived classes implement:
//   - absl::Status check_impl(rx_queues, tx_queues)  [cold path]
//   - void process_impl()                              [hot path]
template <typename Derived>
class PacketProcessorBase {
 public:
  explicit PacketProcessorBase(const dpdk_config::PmdThreadConfig& config)
      : config_(config) {}

  // Cold-path: validate queue assignments before entering the loop.
  // Delegates to Derived::check_impl().
  absl::Status Check() {
    return static_cast<Derived*>(this)->check_impl(
        config_.rx_queues, config_.tx_queues);
  }

  // Hot-path: one iteration of receive → process → transmit.
  // Delegates to Derived::process_impl(). Inlineable when type is known.
  void Process() {
    static_cast<Derived*>(this)->process_impl();
  }

 protected:
  const dpdk_config::PmdThreadConfig& config() const { return config_; }

 private:
  dpdk_config::PmdThreadConfig config_;
};

}  // namespace processor

#endif  // PROCESSOR_PACKET_PROCESSOR_BASE_H_
