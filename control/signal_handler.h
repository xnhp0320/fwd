#ifndef DPDK_CONFIG_CONTROL_SIGNAL_HANDLER_H_
#define DPDK_CONFIG_CONTROL_SIGNAL_HANDLER_H_

#include <functional>

#include "boost/asio/io_context.hpp"
#include "boost/asio/signal_set.hpp"

namespace dpdk_config {

// SignalHandler integrates POSIX signals (SIGINT, SIGTERM) into the event loop.
// It uses boost::asio::signal_set to deliver signals as asynchronous events,
// ensuring thread safety and proper integration with the io_context.
class SignalHandler {
 public:
  SignalHandler(boost::asio::io_context& io_context,
                std::function<void()> shutdown_callback);

  // Start listening for signals.
  void Start();

  // Stop listening for signals.
  void Stop();

 private:
  void StartWait();
  void HandleSignal(const boost::system::error_code& error, int signal_number);

  boost::asio::signal_set signals_;
  std::function<void()> shutdown_callback_;
  bool waiting_ = false;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_SIGNAL_HANDLER_H_
