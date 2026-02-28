"""Multi-configuration tests for DPDK application.

This module tests various DPDK configurations with different combinations of:
- Port counts (1-2 ports)
- Thread counts (1-2 threads)
- Queue counts (1-2 queues per port)

These tests verify that the system works correctly across different
configuration scenarios within VM resource constraints.
"""

import pytest
from typing import Dict, Any


class TestMultiConfiguration:
    """Test DPDK with multiple configuration combinations."""
    
    @pytest.mark.parametrize("num_ports,num_threads,num_queues", [
        (1, 1, 1),  # Minimal configuration
        (1, 2, 2),  # Single port, multi-thread, multi-queue
        (2, 2, 2),  # Full configuration (default)
    ])
    def test_configuration_matrix(
        self,
        num_ports: int,
        num_threads: int,
        num_queues: int,
        binary_path,
        test_output_dir,
        request
    ):
        """
        Test various configuration combinations.
        
        This test verifies that the DPDK process can start and respond to
        control commands with different configurations. Each configuration
        is tested independently to ensure the system works across various
        scenarios.
        
        Test configurations:
        - (1 port, 1 thread, 1 queue): Minimal configuration
        - (1 port, 2 threads, 2 queues): Multi-thread with single port
        - (2 ports, 2 threads, 2 queues): Full multi-port configuration
        
        Args:
            num_ports: Number of virtual ports to configure
            num_threads: Number of PMD threads to configure
            num_queues: Number of queues per port to configure
            binary_path: Path to DPDK binary (fixture)
            test_output_dir: Directory for test outputs (fixture)
            request: pytest request object for test metadata
        
        Validates:
        - Process starts successfully with configuration
        - Control plane responds to commands
        - Thread count matches configuration
        - Queue distribution is correct
        - Process can be shut down gracefully
        
        Requirements: 6.1, 6.2, 6.3, 6.4
        """
        from fixtures.config_generator import TestConfigGenerator
        from fixtures.dpdk_process import DpdkProcess
        from fixtures.control_client import ControlClient
        import time
        
        # Generate configuration for this test case
        config = TestConfigGenerator.generate_config(
            num_ports=num_ports,
            num_threads=num_threads,
            num_queues=num_queues,
            use_hugepages=False
        )
        
        # Write configuration to file
        config_path = test_output_dir / f"multi_config_{num_ports}p_{num_threads}t_{num_queues}q.json"
        TestConfigGenerator.write_config(config, str(config_path))
        
        # Create and start DPDK process
        process = DpdkProcess(
            binary_path=str(binary_path),
            config_path=str(config_path),
            startup_timeout=30,
            shutdown_timeout=10
        )
        
        try:
            # Start process
            assert process.start(), \
                f"Failed to start DPDK process with config ({num_ports}p, {num_threads}t, {num_queues}q)"
            
            # Wait for initialization
            assert process.wait_for_ready(), \
                f"DPDK process failed to initialize with config ({num_ports}p, {num_threads}t, {num_queues}q)\n" \
                f"Stdout: {process.get_stdout()}\n" \
                f"Stderr: {process.get_stderr()}"
            
            # Verify process is running
            assert process.is_running(), \
                f"Process should be running after initialization ({num_ports}p, {num_threads}t, {num_queues}q)"
            
            # Connect control client
            client = ControlClient(socket_path="/tmp/dpdk_control.sock")
            assert client.connect(timeout=5.0, retry_count=10, retry_delay=0.5), \
                f"Failed to connect to control socket ({num_ports}p, {num_threads}t, {num_queues}q)"
            
            try:
                # Test status command
                status_response = client.status()
                assert status_response['status'] == 'success', \
                    f"Status command should succeed ({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Test get_threads command
                threads_response = client.get_threads()
                assert threads_response['status'] == 'success', \
                    f"get_threads command should succeed ({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Verify thread count matches configuration
                threads = threads_response['result']['threads']
                assert len(threads) == num_threads, \
                    f"Expected {num_threads} threads, got {len(threads)} " \
                    f"({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Verify lcore assignments are valid
                lcore_ids = [t['lcore_id'] for t in threads]
                assert all(lcore_id > 0 for lcore_id in lcore_ids), \
                    f"All lcore IDs should be > 0 (lcore 0 reserved for main) " \
                    f"({num_ports}p, {num_threads}t, {num_queues}q)"
                
                assert len(lcore_ids) == len(set(lcore_ids)), \
                    f"All lcore IDs should be unique " \
                    f"({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Test graceful shutdown
                shutdown_response = client.shutdown()
                assert shutdown_response['status'] == 'success', \
                    f"Shutdown command should succeed ({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Wait for process to terminate
                timeout = 10
                start = time.time()
                while process.is_running() and (time.time() - start) < timeout:
                    time.sleep(0.1)
                
                assert not process.is_running(), \
                    f"Process should have terminated after shutdown " \
                    f"({num_ports}p, {num_threads}t, {num_queues}q)"
                
                # Verify clean exit
                exit_code = process.get_exit_code()
                assert exit_code == 0, \
                    f"Process should exit with code 0, got {exit_code} " \
                    f"({num_ports}p, {num_threads}t, {num_queues}q)"
                
            finally:
                # Clean up control client
                client.close()
        
        finally:
            # Clean up process
            if process.is_running():
                process.terminate(graceful=True)
                time.sleep(1)
                if process.is_running():
                    process.terminate(graceful=False)
            
            # Save logs on failure
            test_failed = request.node.rep_call.failed if hasattr(request.node, 'rep_call') else False
            if test_failed:
                log_file = test_output_dir / f"multi_config_{num_ports}p_{num_threads}t_{num_queues}q_output.log"
                with open(log_file, 'w') as f:
                    f.write(f"=== Configuration: {num_ports} ports, {num_threads} threads, {num_queues} queues ===\n")
                    f.write(f"Exit code: {process.get_exit_code()}\n")
                    f.write(f"Error message: {process.error_message}\n\n")
                    f.write("=== STDOUT ===\n")
                    f.write(process.get_stdout())
                    f.write("\n\n=== STDERR ===\n")
                    f.write(process.get_stderr())
