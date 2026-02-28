#ifndef DPDK_CONFIG_CONTROL_UNIX_SOCKET_SERVER_H_
#define DPDK_CONFIG_CONTROL_UNIX_SOCKET_SERVER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "boost/asio/io_context.hpp"
#include "boost/asio/local/stream_protocol.hpp"
#include "boost/asio/streambuf.hpp"

namespace dpdk_config {

// UnixSocketServer manages the Unix domain socket lifecycle and client
// connections. It accepts multiple concurrent connections and reads
// newline-delimited JSON messages from clients.
class UnixSocketServer {
 public:
  using MessageCallback = std::function<void(
      const std::string& message,
      std::function<void(const std::string&)> response_callback)>;

  UnixSocketServer(boost::asio::io_context& io_context,
                   const std::string& socket_path);
  ~UnixSocketServer();

  // Start accepting connections.
  absl::Status Start(MessageCallback callback);

  // Stop accepting new connections and close existing ones.
  void Stop();

 private:
  class Connection;

  void StartAccept();
  void HandleAccept(std::shared_ptr<Connection> conn,
                    const boost::system::error_code& error);
  void RemoveConnection(Connection* conn);

  boost::asio::io_context& io_context_;
  std::string socket_path_;
  std::unique_ptr<boost::asio::local::stream_protocol::acceptor> acceptor_;
  MessageCallback message_callback_;
  std::vector<std::shared_ptr<Connection>> connections_;
  bool accepting_ = false;
};

// Connection manages a single client connection.
// This is a nested class but defined separately for clarity.
class UnixSocketServer::Connection
    : public std::enable_shared_from_this<Connection> {
 public:
  Connection(boost::asio::io_context& io_context,
             UnixSocketServer* server,
             MessageCallback callback);

  boost::asio::local::stream_protocol::socket& Socket();

  void Start();
  void Close();
  void SendResponse(const std::string& response);

 private:
  void StartRead();
  void HandleRead(const boost::system::error_code& error,
                  size_t bytes_transferred);
  void HandleWrite(const boost::system::error_code& error);

  boost::asio::local::stream_protocol::socket socket_;
  UnixSocketServer* server_;  // Not owned
  MessageCallback callback_;
  boost::asio::streambuf read_buffer_;
  std::string write_buffer_;
  bool reading_ = false;
};

}  // namespace dpdk_config

#endif  // DPDK_CONFIG_CONTROL_UNIX_SOCKET_SERVER_H_
