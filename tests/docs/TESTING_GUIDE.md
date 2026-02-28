# DPDK Control Plane Testing Guide

This guide provides instructions for testing the complete DPDK control plane system with Boost.Asio event loop, Unix socket interface, and signal handling.

## Prerequisites

### Hardware/Environment Requirements

The system requires one of the following:

1. **Physical DPDK-compatible NICs**: Intel 82599, X710, or similar
2. **Virtual environment**: QEMU/KVM with virtio or SR-IOV
3. **Test mode**: DPDK with `--no-pci` flag (no actual packet processing)

### Software Requirements

- DPDK installed at `/usr/local/lib/aarch64-linux-gnu/` (or appropriate path)
- Boost.Asio library
- Unix socket client tool: `nc` (netcat) or `socat`
- Root privileges (for DPDK hugepages and device access)

## Building the System

```bash
# Build the main binary
bazel build //:main

# The binary will be at: bazel-bin/main
```

## Test Scenarios

### Scenario 1: Test Mode (No Hardware Required)

This mode tests the control plane without actual DPDK ports.

#### Step 1: Run the binary in test mode

```bash
# Run without configuration file (will fail DPDK init but demonstrates control plane)
sudo ./bazel-bin/main --socket_path=/tmp/dpdk_control.sock --verbose
```

**Expected behavior**: The program will fail during DPDK initialization because no NICs are available. This is expected for test mode.

#### Step 2: Run with no-pci flag (if DPDK supports it)

Create a minimal test configuration:

```json
{
  "core_mask": "0x3",
  "log_level": 7,
  "memory_channels": 2,
  "ports": [],
  "pmd_threads": []
}
```

Save as `test_minimal.json` and run:

```bash
sudo ./bazel-bin/main -i test_minimal.json --socket_path=/tmp/dpdk_control.sock --verbose
```

### Scenario 2: Full System Test (With DPDK Hardware)

This scenario requires actual DPDK-compatible NICs or virtual devices.

#### Step 1: Verify DPDK environment

```bash
# Check hugepages
cat /proc/meminfo | grep Huge

# Bind NICs to DPDK driver (example for Intel NICs)
sudo dpdk-devbind.py --status
sudo dpdk-devbind.py --bind=vfio-pci 0000:01:00.0 0000:01:00.1
```

#### Step 2: Run the main binary

```bash
sudo ./bazel-bin/main -i dpdk.json --socket_path=/tmp/dpdk_control.sock --verbose
```

**Expected output**:
```
Verbose mode enabled
Loaded configuration:
{...}
DPDK initialized successfully
Main thread running on lcore 0 (control plane)
Control plane initialized on socket: /tmp/dpdk_control.sock
Press Ctrl+C to exit...
```

#### Step 3: Connect via Unix socket client

Open a new terminal and connect:

```bash
# Using netcat
nc -U /tmp/dpdk_control.sock

# Or using socat
socat - UNIX-CONNECT:/tmp/dpdk_control.sock
```

#### Step 4: Send test commands

Once connected, send JSON commands (one per line):

**Test 1: Status command**
```json
{"command":"status"}
```

**Expected response**:
```json
{"status":"success","result":{"main_lcore":0,"num_pmd_threads":4}}
```

**Test 2: Get threads command**
```json
{"command":"get_threads"}
```

**Expected response**:
```json
{"status":"success","result":{"threads":[{"lcore_id":1},{"lcore_id":2},{"lcore_id":3},{"lcore_id":4}]}}
```

**Test 3: Invalid JSON**
```json
{invalid json
```

**Expected response**:
```json
{"status":"error","error":"JSON parse error: ..."}
```

**Test 4: Missing command field**
```json
{"params":{}}
```

**Expected response**:
```json
{"status":"error","error":"Missing required field: command"}
```

**Test 5: Unknown command**
```json
{"command":"invalid_command"}
```

**Expected response**:
```json
{"status":"error","error":"Unknown command: invalid_command"}
```

**Test 6: Shutdown command**
```json
{"command":"shutdown"}
```

**Expected response**:
```json
{"status":"success","result":{"message":"Shutdown initiated"}}
```

The server should then gracefully shut down.

#### Step 5: Test signal handling (Ctrl+C)

1. Run the main binary again
2. Connect with a socket client
3. Press Ctrl+C in the main binary terminal

**Expected behavior**:
- The program should print "Shutting down..." or similar
- PMD threads should stop gracefully
- Socket connections should close
- Socket file should be removed
- Program should exit with code 0

#### Step 6: Test multiple concurrent connections

Open 3 terminals and connect from each:

```bash
# Terminal 1
nc -U /tmp/dpdk_control.sock

# Terminal 2
nc -U /tmp/dpdk_control.sock

# Terminal 3
nc -U /tmp/dpdk_control.sock
```

Send commands from each terminal simultaneously and verify all receive responses.

### Scenario 3: Automated Testing Script

Use the provided test script for automated verification:

```bash
./test_control_plane.sh
```

## Verification Checklist

Use this checklist to verify all requirements:

### Event Loop Initialization
- [ ] Control plane creates io_context on main lcore
- [ ] Initialization fails if not on main lcore
- [ ] Event loop remains active until shutdown

### Unix Socket Server
- [ ] Socket created at configured path
- [ ] Existing socket file removed before binding
- [ ] Multiple clients can connect simultaneously
- [ ] Connections cleaned up on disconnect
- [ ] Server continues accepting connections until shutdown

### JSON Command Reception
- [ ] Valid JSON commands are parsed and processed
- [ ] Invalid JSON returns error response
- [ ] Newline-delimited messages work correctly
- [ ] Responses are valid JSON

### Signal Integration
- [ ] SIGINT (Ctrl+C) triggers graceful shutdown
- [ ] SIGTERM triggers graceful shutdown
- [ ] PMD threads stopped before event loop stops
- [ ] Socket closed during shutdown

### Command Protocol
- [ ] "status" command returns system information
- [ ] "get_threads" command returns thread list
- [ ] "shutdown" command initiates graceful shutdown
- [ ] Missing "command" field returns error
- [ ] Unknown commands return error
- [ ] Optional "params" field supported

### Error Handling
- [ ] Socket errors logged appropriately
- [ ] JSON parse errors handled gracefully
- [ ] Command execution errors returned as JSON
- [ ] Initialization errors propagate to main()

### Graceful Shutdown
- [ ] New connections rejected after shutdown initiated
- [ ] In-flight commands complete before shutdown
- [ ] PMD threads stopped via StopAllThreads()
- [ ] PMD threads waited via WaitForThreads()
- [ ] Event loop stops after PMD threads stop
- [ ] Socket file removed on exit

## Troubleshooting

### Issue: "Cannot bind socket: Address already in use"

**Solution**: Remove the existing socket file:
```bash
sudo rm /tmp/dpdk_control.sock
```

### Issue: "DPDK initialization failed"

**Possible causes**:
1. No DPDK-compatible NICs available
2. NICs not bound to DPDK driver
3. Insufficient hugepages
4. Incorrect configuration file

**Solution**: Check DPDK environment setup and configuration file.

### Issue: "Not running on main lcore"

**Solution**: Ensure the main thread is on lcore 0. This should be automatic with DPDK EAL initialization.

### Issue: Socket client cannot connect

**Possible causes**:
1. Socket file doesn't exist
2. Incorrect socket path
3. Permission issues

**Solution**: 
```bash
# Check socket file exists
ls -la /tmp/dpdk_control.sock

# Check permissions
sudo chmod 660 /tmp/dpdk_control.sock
```

## Performance Considerations

- The control plane runs on the main lcore and does not interfere with packet processing
- Socket I/O is asynchronous and non-blocking
- Command processing is lightweight and should not impact PMD thread performance
- Multiple concurrent connections are supported without performance degradation

## Security Considerations

- Unix socket has 0660 permissions (owner and group only)
- No authentication mechanism (relies on filesystem permissions)
- Commands execute with the privileges of the main process
- Consider restricting socket directory permissions in production

## Next Steps

After verifying the system:

1. Run all unit tests: `bazel test //...`
2. Review any test failures
3. Test with actual packet traffic if hardware available
4. Monitor PMD thread performance during control operations
5. Test edge cases (rapid connections, large command volumes, etc.)

## Support

If issues arise during testing:
1. Check the verbose output for detailed error messages
2. Review DPDK logs for initialization issues
3. Verify socket file permissions and path
4. Ensure DPDK environment is properly configured
5. Consult the requirements and design documents in `.kiro/specs/asio-control-loop/`
