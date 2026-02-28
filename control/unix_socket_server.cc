#include "control/unix_socket_server.h"

#include <iostream>
#include <system_error>
#include <unistd.h>

#include "absl/strings/str_cat.h"
#include "boost/asio/read_until.hpp"
#include "boost/asio/write.hpp"

namespace dpdk_config {

// UnixSocketServer implementation

UnixSocketServer::UnixSocketServer(boost::asio::io_context& io_context,
                                   const std::string& socket_path)
    : io_context_(io_context), socket_path_(socket_path) {}

UnixSocketServer::~UnixSocketServer() {
  Stop();
  // Remove socket file on destruction
  if (!socket_path_.empty()) {
    ::unlink(socket_path_.c_str());
  }
}

absl::Status UnixSocketServer::Start(MessageCallback callback) {
  if (accepting_) {
    return absl::FailedPreconditionError("Server is already running");
  }

  message_callback_ = std::move(callback);

  // Remove existing socket file if present
  ::unlink(socket_path_.c_str());

  try {
    // Create acceptor and bind to socket path
    acceptor_ = std::make_unique<boost::asio::local::stream_protocol::acceptor>(
        io_context_);
    
    boost::asio::local::stream_protocol::endpoint endpoint(socket_path_);
    acceptor_->open(endpoint.protocol());
    acceptor_->bind(endpoint);
    acceptor_->listen();

    // Set socket file permissions to 0660
    ::chmod(socket_path_.c_str(), 0660);

    accepting_ = true;
    StartAccept();

    std::cout << "Unix socket server listening on " << socket_path_ << "\n";
    return absl::OkStatus();
  } catch (const boost::system::system_error& e) {
    return absl::InternalError(
        absl::StrCat("Failed to start Unix socket server: ", e.what()));
  }
}

void UnixSocketServer::Stop() {
  if (!accepting_) {
    return;
  }

  accepting_ = false;

  // Close acceptor to stop accepting new connections
  if (acceptor_ && acceptor_->is_open()) {
    boost::system::error_code ec;
    acceptor_->close(ec);
    if (ec) {
      std::cerr << "Error closing acceptor: " << ec.message() << "\n";
    }
  }

  // Close all existing connections
  for (auto& conn : connections_) {
    conn->Close();
  }
  connections_.clear();
}

void UnixSocketServer::StartAccept() {
  if (!accepting_) {
    return;
  }

  // Create a new connection object
  auto conn = std::make_shared<Connection>(io_context_, this, message_callback_);

  // Accept the next connection asynchronously
  acceptor_->async_accept(
      conn->Socket(),
      [this, conn](const boost::system::error_code& error) {
        HandleAccept(conn, error);
      });
}

void UnixSocketServer::HandleAccept(std::shared_ptr<Connection> conn,
                                    const boost::system::error_code& error) {
  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      std::cerr << "Failed to accept connection: " << error.message() << "\n";
    }
    return;
  }

  // Add connection to active connections list
  connections_.push_back(conn);

  // Start reading from the connection
  conn->Start();

  // Continue accepting new connections
  StartAccept();
}

void UnixSocketServer::RemoveConnection(Connection* conn) {
  // Remove connection from the list
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [conn](const std::shared_ptr<Connection>& c) {
                       return c.get() == conn;
                     }),
      connections_.end());
}

// Connection implementation

UnixSocketServer::Connection::Connection(boost::asio::io_context& io_context,
                                         UnixSocketServer* server,
                                         MessageCallback callback)
    : socket_(io_context), server_(server), callback_(std::move(callback)) {}

boost::asio::local::stream_protocol::socket&
UnixSocketServer::Connection::Socket() {
  return socket_;
}

void UnixSocketServer::Connection::Start() {
  StartRead();
}

void UnixSocketServer::Connection::Close() {
  if (socket_.is_open()) {
    boost::system::error_code ec;
    socket_.close(ec);
    if (ec) {
      std::cerr << "Error closing socket: " << ec.message() << "\n";
    }
  }
  reading_ = false;
}

void UnixSocketServer::Connection::SendResponse(const std::string& response) {
  // Add newline to response for newline-delimited protocol
  write_buffer_ = response + "\n";

  // Write response asynchronously
  boost::asio::async_write(
      socket_, boost::asio::buffer(write_buffer_),
      [self = shared_from_this()](const boost::system::error_code& error,
                                   size_t /*bytes_transferred*/) {
        self->HandleWrite(error);
      });
}

void UnixSocketServer::Connection::StartRead() {
  if (reading_) {
    return;
  }

  reading_ = true;

  // Read until newline delimiter
  boost::asio::async_read_until(
      socket_, read_buffer_, '\n',
      [self = shared_from_this()](const boost::system::error_code& error,
                                   size_t bytes_transferred) {
        self->HandleRead(error, bytes_transferred);
      });
}

void UnixSocketServer::Connection::HandleRead(
    const boost::system::error_code& error, size_t bytes_transferred) {
  reading_ = false;

  if (error) {
    if (error != boost::asio::error::operation_aborted &&
        error != boost::asio::error::eof) {
      std::cerr << "Error reading from socket: " << error.message() << "\n";
    }
    // Connection closed or error occurred, clean up
    Close();
    server_->RemoveConnection(this);
    return;
  }

  // Extract the message from the buffer
  std::istream is(&read_buffer_);
  std::string message;
  std::getline(is, message);

  // Remove trailing carriage return if present
  if (!message.empty() && message.back() == '\r') {
    message.pop_back();
  }

  // Invoke callback with message and response callback
  callback_(message, [self = shared_from_this()](const std::string& response) {
    self->SendResponse(response);
  });

  // Continue reading
  StartRead();
}

void UnixSocketServer::Connection::HandleWrite(
    const boost::system::error_code& error) {
  if (error) {
    std::cerr << "Error writing to socket: " << error.message() << "\n";
    Close();
    server_->RemoveConnection(this);
  }
}

}  // namespace dpdk_config
