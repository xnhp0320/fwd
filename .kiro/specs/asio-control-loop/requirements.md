# Requirements Document

## Introduction

This document specifies requirements for implementing a Boost.Asio-based event loop for the control plane of a DPDK packet processing system. The control plane runs on the main lcore and provides a Unix domain socket interface for receiving JSON-formatted commands, while PMD worker threads handle packet processing on other lcores. The event loop integrates signal handling (SIGINT, SIGTERM) to enable graceful shutdown coordination.

## Glossary

- **Control_Plane**: The main thread running on the main lcore that handles system control, configuration, and monitoring operations
- **Event_Loop**: The Boost.Asio io_context that processes asynchronous I/O operations and events on the main thread
- **Unix_Socket_Server**: A Unix domain socket listener that accepts client connections and receives JSON commands
- **Command_Handler**: The component that processes JSON commands received from Unix socket clients
- **Signal_Handler**: The Boost.Asio signal_set component that integrates POSIX signals into the event loop
- **JSON_Parser**: The existing nlohmann/json-based parser (config_parser.cc) used to decode JSON commands
- **JSON_Printer**: The existing nlohmann/json-based printer (config_printer.cc) used to encode JSON responses
- **PMD_Thread_Manager**: The existing component that manages packet processing threads on worker lcores
- **Main_Lcore**: The primary DPDK logical core reserved for control plane operations
- **Worker_Lcore**: A DPDK logical core assigned to packet processing (PMD threads)

## Requirements

### Requirement 1: Event Loop Initialization

**User Story:** As a system operator, I want the control plane to initialize a Boost.Asio event loop on the main lcore, so that asynchronous I/O operations can be processed efficiently.

#### Acceptance Criteria

1. THE Control_Plane SHALL create a single boost::asio::io_context instance on the main lcore during initialization
2. THE Control_Plane SHALL verify that the current thread is running on the main lcore before creating the Event_Loop
3. IF the current thread is not on the main lcore, THEN THE Control_Plane SHALL return an error status
4. THE Event_Loop SHALL remain active until a shutdown signal is received

### Requirement 2: Unix Domain Socket Server

**User Story:** As a client application, I want to connect to the control plane via a Unix domain socket, so that I can send commands to control the packet processing system.

#### Acceptance Criteria

1. THE Unix_Socket_Server SHALL create a Unix domain socket at a configurable file path
2. WHEN the socket file already exists, THE Unix_Socket_Server SHALL remove the existing file before binding
3. THE Unix_Socket_Server SHALL listen for incoming client connections on the Unix domain socket
4. WHEN a client connects, THE Unix_Socket_Server SHALL accept the connection asynchronously
5. THE Unix_Socket_Server SHALL support multiple concurrent client connections
6. WHEN a client disconnects, THE Unix_Socket_Server SHALL clean up the connection resources
7. THE Unix_Socket_Server SHALL continue accepting new connections until shutdown is initiated

### Requirement 3: JSON Command Reception

**User Story:** As a client application, I want to send JSON-formatted commands over the Unix socket, so that I can control and query the packet processing system.

#### Acceptance Criteria

1. WHEN a client connection is established, THE Unix_Socket_Server SHALL read data asynchronously from the socket
2. THE Command_Handler SHALL use the JSON_Parser to decode received data into structured command objects
3. IF the received data is not valid JSON, THEN THE Command_Handler SHALL return an error response to the client
4. THE Command_Handler SHALL support newline-delimited JSON messages (one command per line)
5. WHEN a complete JSON command is received, THE Command_Handler SHALL process the command
6. THE Command_Handler SHALL use the JSON_Printer to encode response data
7. THE Unix_Socket_Server SHALL send the JSON response back to the client asynchronously

### Requirement 4: Signal Integration

**User Story:** As a system operator, I want SIGINT and SIGTERM signals to be handled gracefully within the event loop, so that the system can shut down cleanly without data loss.

#### Acceptance Criteria

1. THE Signal_Handler SHALL register handlers for SIGINT and SIGTERM using boost::asio::signal_set
2. WHEN SIGINT or SIGTERM is received, THE Signal_Handler SHALL initiate graceful shutdown
3. THE Signal_Handler SHALL invoke PMD_Thread_Manager::StopAllThreads() to stop packet processing threads
4. THE Signal_Handler SHALL close the Unix_Socket_Server to reject new connections
5. THE Signal_Handler SHALL allow in-flight command processing to complete before stopping the Event_Loop
6. THE Signal_Handler SHALL stop the Event_Loop after shutdown tasks complete
7. THE Control_Plane SHALL return a success status code after graceful shutdown completes

### Requirement 5: Error Handling and Status Reporting

**User Story:** As a developer, I want all event loop operations to use absl::Status for error reporting, so that error handling is consistent with the existing codebase.

#### Acceptance Criteria

1. THE Event_Loop SHALL return absl::Status from all initialization functions
2. WHEN a socket operation fails, THE Unix_Socket_Server SHALL log the error using boost::system::error_code
3. THE Command_Handler SHALL return absl::StatusOr for command processing results
4. IF the Event_Loop encounters an unrecoverable error, THEN THE Control_Plane SHALL initiate shutdown
5. THE Control_Plane SHALL propagate error status codes to the main() function for process exit code determination

### Requirement 6: Command Protocol Definition

**User Story:** As a client developer, I want a well-defined JSON command protocol, so that I can implement clients that communicate with the control plane.

#### Acceptance Criteria

1. THE Command_Handler SHALL support a JSON command format with required "command" field specifying the operation
2. THE Command_Handler SHALL support an optional "params" field containing command-specific parameters
3. THE Command_Handler SHALL return JSON responses with "status" field indicating success or error
4. WHEN a command succeeds, THE Command_Handler SHALL include a "result" field with command output
5. WHEN a command fails, THE Command_Handler SHALL include an "error" field with a descriptive error message
6. THE Command_Handler SHALL support a "shutdown" command that initiates graceful system shutdown
7. THE Command_Handler SHALL support a "status" command that returns system health information

### Requirement 7: Thread Safety and Lcore Affinity

**User Story:** As a system architect, I want the event loop to respect DPDK lcore affinity, so that the control plane does not interfere with packet processing performance.

#### Acceptance Criteria

1. THE Event_Loop SHALL execute all I/O operations on the main lcore thread
2. THE Control_Plane SHALL NOT spawn additional threads for event loop processing
3. WHEN interacting with PMD_Thread_Manager, THE Control_Plane SHALL use thread-safe interfaces
4. THE Command_Handler SHALL NOT directly access DPDK data structures owned by Worker_Lcore threads
5. THE Control_Plane SHALL use DPDK's rte_lcore_id() to verify execution on the main lcore

### Requirement 8: Configuration and Deployment

**User Story:** As a system operator, I want to configure the Unix socket path, so that I can deploy the system in different environments.

#### Acceptance Criteria

1. THE Control_Plane SHALL accept a Unix socket path as a configuration parameter
2. WHERE no socket path is configured, THE Control_Plane SHALL use a default path "/tmp/dpdk_control.sock"
3. THE Unix_Socket_Server SHALL set appropriate file permissions on the socket file (0660)
4. WHEN the Control_Plane exits, THE Unix_Socket_Server SHALL remove the socket file
5. THE Control_Plane SHALL validate that the socket path directory exists and is writable

### Requirement 9: JSON Parser and Printer Integration

**User Story:** As a developer, I want to reuse the existing JSON parsing and printing infrastructure, so that command handling is consistent with configuration file processing.

#### Acceptance Criteria

1. THE Command_Handler SHALL use nlohmann::json library for JSON parsing
2. THE Command_Handler SHALL follow the same error handling patterns as ConfigParser::ParseString()
3. THE Command_Handler SHALL use json::parse() with exception handling for malformed JSON
4. THE Command_Handler SHALL use json::dump() to serialize response objects
5. FOR ALL valid JSON command objects, parsing then printing then parsing SHALL produce an equivalent object (round-trip property)

### Requirement 10: Graceful Shutdown Coordination

**User Story:** As a system operator, I want the control plane to coordinate shutdown with packet processing threads, so that no packets are lost during system shutdown.

#### Acceptance Criteria

1. WHEN shutdown is initiated, THE Control_Plane SHALL stop accepting new Unix socket connections
2. THE Control_Plane SHALL wait for active command processing to complete before stopping PMD threads
3. THE Control_Plane SHALL invoke PMD_Thread_Manager::StopAllThreads() to signal worker threads
4. THE Control_Plane SHALL invoke PMD_Thread_Manager::WaitForThreads() to wait for worker thread completion
5. WHEN all PMD threads have stopped, THE Control_Plane SHALL stop the Event_Loop
6. THE Control_Plane SHALL complete shutdown within a reasonable timeout period (10 seconds)
7. IF shutdown timeout is exceeded, THEN THE Control_Plane SHALL log a warning and force termination
