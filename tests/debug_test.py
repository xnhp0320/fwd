#!/usr/bin/env python3
"""Debug test to see DPDK process output."""

import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from tests.fixtures.config_generator import TestConfigGenerator
from tests.fixtures.dpdk_process import DpdkProcess

print("=" * 60)
print("DEBUG: Starting DPDK process test")
print("=" * 60)

# Generate config
config = TestConfigGenerator.generate_config(num_ports=1, num_threads=1, num_queues=1)
config_path = "/tmp/debug_dpdk_config.json"
TestConfigGenerator.write_config(config, config_path)
print(f"✓ Config written to: {config_path}")

# Start process
binary_path = "bazel-bin/main"
print(f"✓ Starting DPDK process: sudo {binary_path} -i {config_path}")

process = DpdkProcess(binary_path, config_path, startup_timeout=10)

if not process.start():
    print(f"✗ Failed to start: {process.error_message}")
    sys.exit(1)

print("✓ Process started, waiting for ready...")

# Wait for ready
import time
for i in range(10):
    time.sleep(1)
    stdout = process.get_stdout()
    stderr = process.get_stderr()
    
    print(f"\n--- After {i+1} seconds ---")
    print(f"Running: {process.is_running()}")
    print(f"Stdout lines: {len(process.stdout_lines)}")
    print(f"Stderr lines: {len(process.stderr_lines)}")
    
    if stdout:
        print("\nSTDOUT:")
        print(stdout)
    
    if stderr:
        print("\nSTDERR:")
        print(stderr)
    
    if "Control plane ready" in stdout:
        print("\n✓ Control plane ready!")
        break
    
    if not process.is_running():
        print("\n✗ Process terminated!")
        print(f"Exit code: {process.get_exit_code()}")
        break

# Cleanup
if process.is_running():
    print("\nTerminating process...")
    process.terminate(graceful=False)

print("\n" + "=" * 60)
print("DEBUG: Test complete")
print("=" * 60)
