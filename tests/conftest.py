"""pytest configuration and fixtures for DPDK e2e tests.

This module provides pytest fixtures for automated test setup and teardown,
including DPDK process management, control client connections, and TAP
interface verification.
"""

import pytest
import json
import time
import sys
from pathlib import Path

# Add tests directory to Python path for imports
sys.path.insert(0, str(Path(__file__).parent))

from fixtures.config_generator import TestConfigGenerator
from fixtures.dpdk_process import DpdkProcess
from fixtures.control_client import ControlClient
from fixtures.tap_interface import TapInterfaceManager


@pytest.fixture(scope="session")
def binary_path():
    """
    Path to the DPDK main binary.

    Automatically grants CAP_NET_ADMIN so TAP interfaces can be created
    without running the entire test suite as root.

    Returns:
        Path object pointing to the Bazel-built binary

    Scope: session (shared across all tests)
    """
    import subprocess as _sp

    # Locate Bazel-built binary (relative to project root, not tests dir)
    project_root = Path(__file__).parent.parent
    binary = project_root / "bazel-bin" / "main"

    if not binary.exists():
        pytest.skip(f"Binary not found: {binary}. Run 'bazel build //:main' first.")

    # Grant CAP_NET_ADMIN so the binary can create TAP interfaces without root
    try:
        _sp.run(
            ['sudo', 'setcap', 'cap_net_admin+ep', str(binary)],
            check=True, capture_output=True, text=True
        )
    except _sp.CalledProcessError as e:
        pytest.skip(
            f"Failed to set CAP_NET_ADMIN on {binary}: {e.stderr.strip()}. "
            "Run tests with sudo access or manually: "
            f"sudo setcap cap_net_admin+ep {binary}"
        )

    return binary


@pytest.fixture(scope="session")
def test_output_dir(tmp_path_factory):
    """
    Directory for test outputs and artifacts.
    
    Returns:
        Path object for temporary test directory
    
    Scope: session (shared across all tests)
    """
    return tmp_path_factory.mktemp("dpdk_tests")


@pytest.fixture
def test_config(test_output_dir, request):
    """
    Generate test configuration based on test parameters.
    
    This fixture automatically generates a dpdk.json configuration file
    with net_tap virtual PMD based on test parameters. Tests can use
    pytest.mark.parametrize to specify configuration parameters.
    
    Parameters (from test markers):
        num_threads: Number of PMD threads (default: 2)
        num_queues: Number of queues per port (default: 2)
        num_ports: Number of virtual ports (default: 2)
    
    Returns:
        Path to generated configuration file
    
    Usage:
        @pytest.mark.parametrize("num_threads,num_queues", [(1,1), (2,2)])
        def test_something(test_config):
            # test_config is automatically generated with specified parameters
    
    Scope: function (new config for each test)
    """
    # Extract parameters from test markers or use defaults
    params = getattr(request, 'param', {})
    
    # Support both dict params and individual markers
    if isinstance(params, dict):
        num_threads = params.get('num_threads', 2)
        num_queues = params.get('num_queues', 2)
        num_ports = params.get('num_ports', 2)
    else:
        # Try to get from test node markers
        num_threads = 2
        num_queues = 2
        num_ports = 2
        
        # Check for custom markers
        for marker in request.node.iter_markers():
            if marker.name == 'config':
                num_threads = marker.kwargs.get('num_threads', num_threads)
                num_queues = marker.kwargs.get('num_queues', num_queues)
                num_ports = marker.kwargs.get('num_ports', num_ports)
    
    # Generate configuration
    config = TestConfigGenerator.generate_config(
        num_ports=num_ports,
        num_threads=num_threads,
        num_queues=num_queues,
        use_hugepages=False  # Always disable hugepages for testing
    )
    
    # Write to file with unique name
    config_path = test_output_dir / f"dpdk_test_{request.node.name}.json"
    TestConfigGenerator.write_config(config, str(config_path))
    
    return config_path


@pytest.fixture
def dpdk_process(binary_path, test_config, test_output_dir, request):
    """
    DPDK process fixture with automatic lifecycle management.
    
    This fixture provides:
    - Automatic process launch with test configuration
    - Startup verification (waits for "Control plane ready")
    - Output capture for diagnostics
    - Automatic cleanup on test completion
    - Log preservation on test failure
    
    Returns:
        DpdkProcess instance (already started and ready)
    
    Scope: function (new process for each test)
    """
    process = DpdkProcess(
        binary_path=str(binary_path),
        config_path=str(test_config),
        startup_timeout=30,
        shutdown_timeout=10
    )
    
    # Start process
    if not process.start():
        pytest.fail(f"Failed to start DPDK process: {process.error_message}")
    
    # Wait for initialization
    if not process.wait_for_ready():
        stdout = process.get_stdout()
        stderr = process.get_stderr()
        error_msg = process.error_message
        
        # Terminate process before failing
        if process.is_running():
            process.terminate(graceful=False)
        
        pytest.fail(
            f"DPDK process failed to initialize: {error_msg}\n"
            f"Stdout:\n{stdout}\n"
            f"Stderr:\n{stderr}"
        )
    
    yield process
    
    # Cleanup
    if process.is_running():
        # Try graceful shutdown first
        process.terminate(graceful=True)
        time.sleep(1)
        
        # Force kill if still running
        if process.is_running():
            process.terminate(graceful=False)
    
    # Save logs on failure
    test_failed = request.node.rep_call.failed if hasattr(request.node, 'rep_call') else False
    if test_failed:
        log_file = test_output_dir / f"{request.node.name}_output.log"
        with open(log_file, 'w') as f:
            f.write("=== DPDK Process Output ===\n")
            f.write(f"Exit code: {process.get_exit_code()}\n")
            f.write(f"Error message: {process.error_message}\n\n")
            f.write("=== STDOUT ===\n")
            f.write(process.get_stdout())
            f.write("\n\n=== STDERR ===\n")
            f.write(process.get_stderr())


@pytest.fixture
def control_client(dpdk_process):
    """
    Control client fixture with automatic connection management.
    
    This fixture provides:
    - Automatic Unix socket connection with retry logic
    - Connection verification
    - Automatic cleanup on test completion
    
    Returns:
        ControlClient instance (already connected)
    
    Scope: function (new client for each test)
    
    Dependencies:
        Requires dpdk_process fixture (process must be running)
    """
    client = ControlClient(socket_path="/tmp/dpdk_control.sock")
    
    # Connect to socket with retry
    if not client.connect(timeout=5.0, retry_count=10, retry_delay=0.5):
        pytest.fail("Failed to connect to control socket")
    
    yield client
    
    # Cleanup
    client.close()


@pytest.fixture
def tap_interfaces(test_config, dpdk_process):
    """
    TAP interface fixture for verification and cleanup checking.
    
    This fixture provides:
    - Automatic verification that TAP interfaces are created
    - Interface configuration information
    - Cleanup verification after test completion
    
    Returns:
        List of interface names (e.g., ['dtap0', 'dtap1'])
    
    Scope: function (verifies interfaces for each test)
    
    Dependencies:
        Requires test_config and dpdk_process fixtures
    """
    # Determine expected interfaces from config
    with open(test_config) as f:
        config = json.load(f)
    
    # Extract interface names from vdev parameters
    interfaces = []
    for param_pair in config.get('additional_params', []):
        if len(param_pair) >= 2 and param_pair[0] == '--vdev' and 'net_tap' in param_pair[1]:
            # Parse "net_tap0,iface=dtap0" to extract "dtap0"
            parts = param_pair[1].split(',')
            for part in parts:
                if part.startswith('iface='):
                    interfaces.append(part.split('=')[1])
    
    # Wait for interfaces to appear
    for iface in interfaces:
        if not TapInterfaceManager.wait_for_interface(iface, timeout=10):
            pytest.fail(f"TAP interface {iface} did not appear within timeout")
    
    # Verify all interfaces exist
    if not TapInterfaceManager.verify_interfaces(interfaces):
        missing = [i for i in interfaces if not TapInterfaceManager.interface_exists(i)]
        pytest.fail(f"TAP interfaces not found: {missing}")
    
    yield interfaces
    
    # Note: Cleanup verification happens after dpdk_process fixture cleanup
    # We don't verify cleanup here because the process fixture runs after this one


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    """
    Hook to capture test results for failure diagnostics.
    
    This hook stores test results on the test node so fixtures can
    access them during teardown to save logs on failure.
    """
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)
