#!/bin/bash

# Manual Test Commands for DPDK Control Plane
# This script provides example commands to test the control plane manually

SOCKET_PATH="/tmp/dpdk_control.sock"

echo "========================================="
echo "DPDK Control Plane Manual Test Commands"
echo "========================================="
echo ""
echo "Socket path: $SOCKET_PATH"
echo ""
echo "Prerequisites:"
echo "1. Build the binary: bazel build //:main"
echo "2. Grant TAP capability: sudo setcap cap_net_admin+ep ./bazel-bin/main"
echo "3. Run the main binary in another terminal:"
echo "   ./bazel-bin/main -i dpdk.json --socket_path=$SOCKET_PATH --verbose"
echo ""
echo "========================================="
echo ""

# Function to send a command
send_cmd() {
    local name="$1"
    local cmd="$2"
    
    echo "----------------------------------------"
    echo "Test: $name"
    echo "Command: $cmd"
    echo "Response:"
    echo "$cmd" | socat - UNIX-CONNECT:$SOCKET_PATH
    echo ""
}

# Check if socket exists
if [ ! -S "$SOCKET_PATH" ]; then
    echo "ERROR: Socket not found at $SOCKET_PATH"
    echo "  Make sure the main binary is running first."
    echo ""
    echo "Run in another terminal:"
    echo "  sudo setcap cap_net_admin+ep ./bazel-bin/main"
    echo "  ./bazel-bin/main -i dpdk.json --socket_path=$SOCKET_PATH --verbose"
    exit 1
fi

# Check if socat is available
if ! command -v socat &> /dev/null; then
    echo "ERROR: socat not found. Install it with:"
    echo "  sudo apt-get install socat"
    echo ""
    echo "Alternatively, use nc (netcat):"
    echo "  echo '{\"command\":\"status\"}' | nc -U $SOCKET_PATH"
    exit 1
fi

echo "Running tests..."
echo ""

# Test 1: Status command
send_cmd "Status Command" '{"command":"status"}'

# Test 2: Get threads command
send_cmd "Get Threads Command" '{"command":"get_threads"}'

# Test 3: Status with empty params
send_cmd "Status with Empty Params" '{"command":"status","params":{}}'

# Test 4: Invalid JSON
send_cmd "Invalid JSON (should return error)" '{invalid json'

# Test 5: Missing command field
send_cmd "Missing Command Field (should return error)" '{"params":{}}'

# Test 6: Unknown command
send_cmd "Unknown Command (should return error)" '{"command":"unknown_cmd"}'

# Test 7: Command with extra fields (should be ignored)
send_cmd "Command with Extra Fields" '{"command":"status","extra":"ignored"}'

echo "----------------------------------------"
echo ""
echo "All test commands sent!"
echo ""
echo "To test shutdown, run:"
echo "  echo '{\"command\":\"shutdown\"}' | socat - UNIX-CONNECT:$SOCKET_PATH"
echo ""
echo "To test signal handling, press Ctrl+C in the main binary terminal"
echo ""
