#!/bin/bash

# DPDK Control Plane Automated Test Script
# This script tests the control plane functionality via Unix socket

set -e

SOCKET_PATH="/tmp/dpdk_control_test.sock"
TEST_CONFIG="test_minimal.json"
TIMEOUT=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

echo "========================================="
echo "DPDK Control Plane Test Suite"
echo "========================================="
echo ""

# Function to print test results
pass_test() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((TESTS_PASSED++))
}

fail_test() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    echo -e "  ${RED}Error: $2${NC}"
    ((TESTS_FAILED++))
}

info() {
    echo -e "${YELLOW}ℹ INFO${NC}: $1"
}

# Function to send command and get response
send_command() {
    local cmd="$1"
    local response
    
    # Send command via socat with timeout
    response=$(echo "$cmd" | timeout $TIMEOUT socat - UNIX-CONNECT:$SOCKET_PATH 2>&1 || true)
    echo "$response"
}

# Function to check if socket exists
check_socket() {
    if [ -S "$SOCKET_PATH" ]; then
        return 0
    else
        return 1
    fi
}

# Cleanup function
cleanup() {
    info "Cleaning up..."
    
    # Remove test socket if it exists
    if [ -S "$SOCKET_PATH" ]; then
        rm -f "$SOCKET_PATH"
    fi
    
    # Remove test config if it exists
    if [ -f "$TEST_CONFIG" ]; then
        rm -f "$TEST_CONFIG"
    fi
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Test 1: Check if binary exists
echo "Test 1: Binary existence"
if [ -f "bazel-bin/main" ]; then
    pass_test "Binary exists at bazel-bin/main"
else
    fail_test "Binary not found" "Run 'bazel build //:main' first"
    exit 1
fi

# Test 2: Check if socket client is available
echo ""
echo "Test 2: Socket client availability"
if command -v socat &> /dev/null; then
    pass_test "socat is available"
elif command -v nc &> /dev/null; then
    pass_test "nc (netcat) is available"
    info "Note: This script uses socat. Install it for better compatibility."
else
    fail_test "No socket client found" "Install socat or nc (netcat)"
    exit 1
fi

# Test 3: Create minimal test configuration
echo ""
echo "Test 3: Create test configuration"
cat > "$TEST_CONFIG" << 'EOF'
{
  "core_mask": "0x3",
  "log_level": 7,
  "memory_channels": 2,
  "ports": [],
  "pmd_threads": []
}
EOF

if [ -f "$TEST_CONFIG" ]; then
    pass_test "Test configuration created"
else
    fail_test "Failed to create test configuration" "Check write permissions"
    exit 1
fi

# Test 4: Check if we can run with sudo
echo ""
echo "Test 4: Privilege check"

# Grant CAP_NET_ADMIN so the binary can create TAP interfaces without root
sudo setcap cap_net_admin+ep ./bazel-bin/main 2>/dev/null
if getcap ./bazel-bin/main 2>/dev/null | grep -q cap_net_admin; then
    pass_test "CAP_NET_ADMIN granted to binary"
else
    info "Could not set CAP_NET_ADMIN. TAP creation may fail without root."
fi

# Test 5: Start the control plane in background
echo ""
echo "Test 5: Start control plane"
info "Starting control plane in background..."

# Note: This will likely fail DPDK init without hardware, but we can still test the socket
timeout 10 ./bazel-bin/main -i "$TEST_CONFIG" --socket_path="$SOCKET_PATH" --verbose > /tmp/control_plane.log 2>&1 &
MAIN_PID=$!

# Wait a bit for startup
sleep 2

# Check if process is still running or if socket was created
if check_socket; then
    pass_test "Control plane started and socket created"
elif kill -0 $MAIN_PID 2>/dev/null; then
    info "Process running but socket not yet created (may need DPDK hardware)"
    # Wait a bit more
    sleep 3
    if check_socket; then
        pass_test "Socket created after delay"
    else
        fail_test "Socket not created" "Check /tmp/control_plane.log for errors"
        cat /tmp/control_plane.log
        kill $MAIN_PID 2>/dev/null || true
        exit 1
    fi
else
    fail_test "Control plane failed to start" "Check /tmp/control_plane.log for errors"
    cat /tmp/control_plane.log
    exit 1
fi

# Test 6: Test status command
echo ""
echo "Test 6: Status command"
response=$(send_command '{"command":"status"}')
if echo "$response" | grep -q '"status":"success"'; then
    pass_test "Status command succeeded"
    info "Response: $response"
else
    fail_test "Status command failed" "Response: $response"
fi

# Test 7: Test get_threads command
echo ""
echo "Test 7: Get threads command"
response=$(send_command '{"command":"get_threads"}')
if echo "$response" | grep -q '"status":"success"'; then
    pass_test "Get threads command succeeded"
    info "Response: $response"
else
    fail_test "Get threads command failed" "Response: $response"
fi

# Test 8: Test invalid JSON
echo ""
echo "Test 8: Invalid JSON handling"
response=$(send_command '{invalid json')
if echo "$response" | grep -q '"status":"error"'; then
    pass_test "Invalid JSON properly rejected"
    info "Response: $response"
else
    fail_test "Invalid JSON not handled" "Response: $response"
fi

# Test 9: Test missing command field
echo ""
echo "Test 9: Missing command field"
response=$(send_command '{"params":{}}')
if echo "$response" | grep -q '"status":"error"'; then
    pass_test "Missing command field detected"
    info "Response: $response"
else
    fail_test "Missing command field not detected" "Response: $response"
fi

# Test 10: Test unknown command
echo ""
echo "Test 10: Unknown command"
response=$(send_command '{"command":"invalid_command"}')
if echo "$response" | grep -q '"status":"error"'; then
    pass_test "Unknown command rejected"
    info "Response: $response"
else
    fail_test "Unknown command not rejected" "Response: $response"
fi

# Test 11: Test multiple concurrent connections
echo ""
echo "Test 11: Multiple concurrent connections"
(send_command '{"command":"status"}' > /tmp/conn1.txt) &
(send_command '{"command":"status"}' > /tmp/conn2.txt) &
(send_command '{"command":"status"}' > /tmp/conn3.txt) &
wait

conn1=$(cat /tmp/conn1.txt)
conn2=$(cat /tmp/conn2.txt)
conn3=$(cat /tmp/conn3.txt)

if echo "$conn1" | grep -q '"status":"success"' && \
   echo "$conn2" | grep -q '"status":"success"' && \
   echo "$conn3" | grep -q '"status":"success"'; then
    pass_test "Multiple concurrent connections handled"
else
    fail_test "Concurrent connections failed" "Check individual responses"
fi

rm -f /tmp/conn1.txt /tmp/conn2.txt /tmp/conn3.txt

# Test 12: Test graceful shutdown via command
echo ""
echo "Test 12: Graceful shutdown via command"
response=$(send_command '{"command":"shutdown"}')
if echo "$response" | grep -q '"status":"success"'; then
    pass_test "Shutdown command accepted"
    info "Response: $response"
    
    # Wait for process to exit
    sleep 2
    if ! kill -0 $MAIN_PID 2>/dev/null; then
        pass_test "Process exited gracefully"
    else
        fail_test "Process did not exit" "Killing process"
        kill $MAIN_PID 2>/dev/null || true
    fi
else
    fail_test "Shutdown command failed" "Response: $response"
    kill $MAIN_PID 2>/dev/null || true
fi

# Test 13: Verify socket cleanup
echo ""
echo "Test 13: Socket cleanup"
sleep 1
if ! check_socket; then
    pass_test "Socket file removed after shutdown"
else
    fail_test "Socket file not removed" "Manual cleanup required"
    rm -f "$SOCKET_PATH"
fi

# Print summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. Check output above.${NC}"
    exit 1
fi
