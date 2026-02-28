#include "control/signal_handler.h"

#include <csignal>
#include <iostream>

#include "boost/system/error_code.hpp"

namespace dpdk_config {

SignalHandler::SignalHandler(boost::asio::io_context& io_context,
                             std::function<void()> shutdown_callback)
    : signals_(io_context, SIGINT, SIGTERM),
      shutdown_callback_(std::move(shutdown_callback)) {}

void SignalHandler::Start() {
  if (!waiting_) {
    StartWait();
  }
}

void SignalHandler::Stop() {
  if (waiting_) {
    waiting_ = false;
    boost::system::error_code ec;
    signals_.cancel(ec);
    if (ec) {
      std::cerr << "Failed to cancel signal handlers: " << ec.message() << "\n";
    }
  }
}

void SignalHandler::StartWait() {
  waiting_ = true;
  signals_.async_wait([this](const boost::system::error_code& error,
                             int signal_number) {
    HandleSignal(error, signal_number);
  });
}

void SignalHandler::HandleSignal(const boost::system::error_code& error,
                                 int signal_number) {
  if (error) {
    // Operation was cancelled or another error occurred
    if (error != boost::asio::error::operation_aborted) {
      std::cerr << "Signal handler error: " << error.message() << "\n";
    }
    waiting_ = false;
    return;
  }

  // Signal received
  const char* signal_name = (signal_number == SIGINT) ? "SIGINT" : "SIGTERM";
  std::cout << "Received " << signal_name << ", initiating graceful shutdown\n";

  // Invoke the shutdown callback
  if (shutdown_callback_) {
    shutdown_callback_();
  }

  // Note: We don't call StartWait() again because shutdown is in progress
  waiting_ = false;
}

}  // namespace dpdk_config
