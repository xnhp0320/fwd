# Implementation Plan: Asio Control Loop

## Overview

This plan implements a Boost.Asio-based event loop for the control plane that provides a Unix domain socket interface for JSON commands and integrates POSIX signal handling. The implementation follows a bottom-up approach, building and testing individual components before integrating them into the complete control plane.

## Tasks

- [x] 1. Create control plane directory structure and headers
  - Create `control/` directory under project root
  - Create header files for all components (control_plane.h, command_handler.h, unix_socket_server.h, signal_handler.h)
  - Define class interfaces matching the design document
  - _Requirements: 1.1, 7.1_

- [ ] 2. Implement CommandHandler for JSON command processing
  - [x] 2.1 Implement CommandHandler class with JSON parsing
    - Implement ParseCommand() using nlohmann::json
    - Implement FormatResponse() for success and error responses
    - Implement ExecuteCommand() dispatcher
    - Handle JSON parsing errors gracefully
    - _Requirements: 3.2, 3.3, 6.1, 6.2, 6.3, 6.4, 6.5, 9.1, 9.2, 9.3, 9.4_
  
  - [x] 2.2 Implement command handlers (shutdown, status, get_threads)
    - Implement HandleShutdown() to invoke shutdown callback
    - Implement HandleStatus() to return system information
    - Implement HandleGetThreads() to query PMDThreadManager
    - Handle unknown commands with error responses
    - _Requirements: 6.6, 6.7_
  
  - [ ]* 2.3 Write unit tests for CommandHandler
    - Test valid command parsing and execution
    - Test invalid JSON handling
    - Test missing command field
    - Test unknown command handling
    - Test each command (shutdown, status, get_threads)
    - Test response formatting
    - _Requirements: 3.3, 6.1, 6.2, 6.3, 6.4, 6.5, 9.2_
  
  - [ ]* 2.4 Write property test for command round-trip serialization
    - **Property 14: Command Round-Trip Serialization**
    - **Validates: Requirements 9.5**

- [ ] 3. Implement UnixSocketServer for async socket I/O
  - [x] 3.1 Implement UnixSocketServer class
    - Implement constructor with io_context and socket_path
    - Implement Start() to create socket, remove existing file, and bind
    - Implement Stop() to close acceptor and all connections
    - Implement StartAccept() for async connection acceptance
    - Implement HandleAccept() callback
    - Implement RemoveConnection() for cleanup
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.7, 8.1, 8.4_
  
  - [x] 3.2 Implement Connection class for per-client I/O
    - Implement Connection constructor with socket and callback
    - Implement Start() to begin reading
    - Implement StartRead() with async_read_until for newline delimiter
    - Implement HandleRead() to parse messages and invoke callback
    - Implement SendResponse() with async_write
    - Implement HandleWrite() callback
    - Implement Close() for cleanup
    - _Requirements: 3.1, 3.4, 3.7, 2.5, 2.6_
  
  - [ ]* 3.3 Write unit tests for UnixSocketServer
    - Test socket creation and binding
    - Test existing socket file removal
    - Test connection acceptance
    - Test message reading with newline delimiter
    - Test response writing
    - Test multiple concurrent connections
    - Test connection cleanup on disconnect
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.4, 3.7_
  
  - [ ]* 3.4 Write property tests for UnixSocketServer
    - **Property 2: Socket Path Configuration**
    - **Validates: Requirements 2.1, 8.1**
    - **Property 3: Concurrent Connection Support**
    - **Validates: Requirements 2.5**
    - **Property 4: Connection Cleanup**
    - **Validates: Requirements 2.6**
    - **Property 5: Newline-Delimited Message Parsing**
    - **Validates: Requirements 3.4**

- [ ] 4. Implement SignalHandler for signal integration
  - [x] 4.1 Implement SignalHandler class
    - Implement constructor with io_context and shutdown callback
    - Implement Start() to register SIGINT and SIGTERM with signal_set
    - Implement StartWait() for async signal waiting
    - Implement HandleSignal() to invoke shutdown callback
    - Implement Stop() to cancel signal waiting
    - _Requirements: 4.1, 4.2_
  
  - [ ]* 4.2 Write unit tests for SignalHandler
    - Test signal registration
    - Test signal delivery triggers callback
    - Test multiple signals handled correctly
    - _Requirements: 4.1, 4.2_
  
  - [ ]* 4.3 Write property test for signal-triggered shutdown
    - **Property 11: Signal-Triggered Shutdown**
    - **Validates: Requirements 4.2**

- [x] 5. Checkpoint - Ensure component tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 6. Implement ControlPlane orchestrator
  - [x] 6.1 Implement ControlPlane class initialization
    - Implement constructor taking PMDThreadManager pointer
    - Implement Initialize() to verify main lcore with rte_lcore_id()
    - Create io_context instance
    - Initialize CommandHandler with shutdown callback
    - Initialize UnixSocketServer with socket path
    - Initialize SignalHandler with shutdown callback
    - Validate socket path directory exists and is writable
    - Return error status if not on main lcore
    - _Requirements: 1.1, 1.2, 1.3, 7.1, 7.5, 8.1, 8.2, 8.5_
  
  - [x] 6.2 Implement ControlPlane Run() and Shutdown()
    - Implement Run() to start all components and run io_context
    - Implement Shutdown() to coordinate graceful shutdown
    - Stop accepting new connections
    - Stop PMD threads via PMDThreadManager::StopAllThreads()
    - Wait for PMD threads via PMDThreadManager::WaitForThreads()
    - Stop io_context
    - Implement shutdown timeout (10 seconds default)
    - Log warning if timeout exceeded
    - _Requirements: 1.4, 4.3, 4.4, 4.5, 4.6, 4.7, 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7_
  
  - [ ]* 6.3 Write unit tests for ControlPlane
    - Test initialization on main lcore succeeds
    - Test initialization on non-main lcore fails
    - Test shutdown coordination sequence
    - Test socket path validation
    - _Requirements: 1.2, 1.3, 8.5_
  
  - [ ]* 6.4 Write property tests for ControlPlane
    - **Property 1: Lcore Affinity Enforcement**
    - **Validates: Requirements 1.2, 1.3**
    - **Property 12: Event Loop Thread Affinity**
    - **Validates: Requirements 7.1**
    - **Property 13: Socket Path Validation**
    - **Validates: Requirements 8.5**

- [ ] 7. Update Bazel BUILD files
  - [x] 7.1 Create control/BUILD file
    - Add cc_library for command_handler
    - Add cc_library for unix_socket_server
    - Add cc_library for signal_handler
    - Add cc_library for control_plane
    - Add dependencies: nlohmann_json, boost.asio, abseil-cpp, dpdk_lib, pmd_thread_manager
    - Add cc_test targets for all unit tests
    - _Requirements: 5.1_
  
  - [x] 7.2 Update root BUILD file
    - Add dependency on //control:control_plane to main binary
    - Ensure boost.asio dependency is present
    - _Requirements: 5.1_

- [ ] 8. Integrate ControlPlane into main.cc
  - [x] 8.1 Replace signal handler and main loop
    - Remove global signal_handler function
    - Remove force_quit atomic flag
    - Remove sleep loop in main()
    - Create ControlPlane instance after DpdkInitializer
    - Configure socket path (use default or add flag)
    - Call ControlPlane::Initialize()
    - Call ControlPlane::Run() to start event loop
    - Handle initialization and runtime errors
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 4.1, 4.2, 5.1, 5.5, 8.1, 8.2_
  
  - [x] 8.2 Add socket_path command-line flag
    - Add ABSL_FLAG for socket_path with default "/tmp/dpdk_control.sock"
    - Pass socket_path to ControlPlane::Initialize()
    - _Requirements: 8.1, 8.2_

- [x] 9. Checkpoint - Ensure integration compiles and basic tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ]* 10. Write integration tests
  - [ ]* 10.1 Write full event loop lifecycle test
    - Test complete initialization, command processing, and shutdown
    - Use real Unix socket with test socket path
    - Send commands via socket client
    - Verify responses
    - Trigger shutdown and verify cleanup
    - _Requirements: 1.1, 1.4, 2.1, 3.1, 3.5, 4.2, 10.1, 10.5_
  
  - [ ]* 10.2 Write property tests for error handling
    - **Property 6: Invalid JSON Error Handling**
    - **Validates: Requirements 3.3, 9.2**
    - **Property 7: Command Processing**
    - **Validates: Requirements 3.5**
    - **Property 8: Response Structure Completeness**
    - **Validates: Requirements 6.3, 6.4, 6.5**
    - **Property 9: Command Field Validation**
    - **Validates: Requirements 6.1**
    - **Property 10: Optional Parameters Support**
    - **Validates: Requirements 6.2**
    - **Property 15: Initialization Status Propagation**
    - **Validates: Requirements 5.1**
    - **Property 16: Socket Error Logging**
    - **Validates: Requirements 5.2**

- [ ] 11. Final checkpoint - Verify complete system
  - Build and run the main binary with a test configuration
  - Connect via Unix socket client (e.g., `nc -U /tmp/dpdk_control.sock`)
  - Send test commands and verify responses
  - Test signal handling (Ctrl+C)
  - Verify graceful shutdown
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties
- Unit tests validate specific examples and edge cases
- The implementation uses C++ with Boost.Asio and follows existing codebase patterns
- All components integrate with existing infrastructure (PMDThreadManager, ConfigParser, ConfigPrinter)
