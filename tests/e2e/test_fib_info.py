"""E2E tests for the get_fib_info control command.

Verifies that the FIB information command returns correct rule counts
and memory usage, both when a FIB is loaded and when no FIB is configured.
"""

import pytest
import time
from pathlib import Path

from fixtures.config_generator import TestConfigGenerator
from fixtures.dpdk_process import DpdkProcess
from fixtures.control_client import ControlClient

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
FIB_FILE = PROJECT_ROOT / "fib" / "ipv4_test_fib.txt"


def count_fib_entries(path: Path) -> int:
    """Count the number of prefix entries in a FIB file."""
    count = 0
    with open(path) as f:
        while True:
            ip_line = f.readline()
            if not ip_line:
                break
            ip_line = ip_line.strip()
            cidr_line = f.readline()
            if not cidr_line:
                break
            if ip_line:
                count += 1
    return count


# ---------------------------------------------------------------------------
# LPM config with FIB loaded
# ---------------------------------------------------------------------------

LPM_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "lpm_forwarding",
}


@pytest.fixture
def fib_test_config(test_output_dir, request):
    """Generate config with fib_file field added."""
    import json

    config = TestConfigGenerator.generate_config(
        num_ports=LPM_CONFIG["num_ports"],
        num_threads=LPM_CONFIG["num_threads"],
        num_rx_queues=LPM_CONFIG["num_rx_queues"],
        num_tx_queues=LPM_CONFIG["num_tx_queues"],
        use_hugepages=False,
        processor_name=LPM_CONFIG["processor_name"],
        fib_file=str(FIB_FILE),
        fib_algorithm="lpm",
    )
    config["additional_params"].append(["-m", "512"])

    config_path = test_output_dir / f"dpdk_fib_{request.node.name}.json"
    TestConfigGenerator.write_config(config, str(config_path))
    return config_path


@pytest.fixture
def fib_dpdk_process(binary_path, fib_test_config, test_output_dir, request):
    """Launch DPDK with the FIB config."""
    process = DpdkProcess(
        binary_path=str(binary_path),
        config_path=str(fib_test_config),
        startup_timeout=30,
        shutdown_timeout=10,
    )

    if not process.start():
        pytest.fail(f"Failed to start DPDK process: {process.error_message}")

    if not process.wait_for_ready():
        stdout = process.get_stdout()
        stderr = process.get_stderr()
        if process.is_running():
            process.terminate(graceful=False)
        pytest.fail(
            f"DPDK process failed to initialize: {process.error_message}\n"
            f"Stdout:\n{stdout}\nStderr:\n{stderr}"
        )

    yield process

    if process.is_running():
        process.terminate(graceful=True)
        time.sleep(1)
        if process.is_running():
            process.terminate(graceful=False)


@pytest.fixture
def fib_control_client(fib_dpdk_process):
    """Control client connected to the FIB DPDK process."""
    client = ControlClient(socket_path="/tmp/dpdk_control.sock")
    if not client.connect(timeout=5.0, retry_count=10, retry_delay=0.5):
        pytest.fail("Failed to connect to control socket")
    yield client
    client.close()


# ---------------------------------------------------------------------------
# No-FIB config (simple forwarding, no fib_file)
# ---------------------------------------------------------------------------

NO_FIB_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
}


@pytest.fixture
def no_fib_test_config(test_output_dir, request):
    """Generate config without fib_file."""
    config = TestConfigGenerator.generate_config(
        num_ports=NO_FIB_CONFIG["num_ports"],
        num_threads=NO_FIB_CONFIG["num_threads"],
        num_rx_queues=NO_FIB_CONFIG["num_rx_queues"],
        num_tx_queues=NO_FIB_CONFIG["num_tx_queues"],
        use_hugepages=False,
    )

    config_path = test_output_dir / f"dpdk_nofib_{request.node.name}.json"
    TestConfigGenerator.write_config(config, str(config_path))
    return config_path


@pytest.fixture
def no_fib_dpdk_process(binary_path, no_fib_test_config, test_output_dir, request):
    """Launch DPDK without FIB."""
    process = DpdkProcess(
        binary_path=str(binary_path),
        config_path=str(no_fib_test_config),
        startup_timeout=30,
        shutdown_timeout=10,
    )

    if not process.start():
        pytest.fail(f"Failed to start DPDK process: {process.error_message}")

    if not process.wait_for_ready():
        stdout = process.get_stdout()
        stderr = process.get_stderr()
        if process.is_running():
            process.terminate(graceful=False)
        pytest.fail(
            f"DPDK process failed to initialize: {process.error_message}\n"
            f"Stdout:\n{stdout}\nStderr:\n{stderr}"
        )

    yield process

    if process.is_running():
        process.terminate(graceful=True)
        time.sleep(1)
        if process.is_running():
            process.terminate(graceful=False)


@pytest.fixture
def no_fib_control_client(no_fib_dpdk_process):
    """Control client connected to the no-FIB DPDK process."""
    client = ControlClient(socket_path="/tmp/dpdk_control.sock")
    if not client.connect(timeout=5.0, retry_count=10, retry_delay=0.5):
        pytest.fail("Failed to connect to control socket")
    yield client
    client.close()


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestFibInfo:
    """E2E tests for the get_fib_info command."""

    def test_fib_info_with_loaded_fib(self, fib_dpdk_process, fib_control_client):
        """Verify get_fib_info returns correct rule count when FIB is loaded."""
        response = fib_control_client.get_fib_info()
        assert response["status"] == "success", f"Expected success: {response}"

        result = response["result"]
        expected_count = count_fib_entries(FIB_FILE)

        assert result["rules_count"] == expected_count, (
            f"Expected {expected_count} rules, got {result['rules_count']}"
        )
        assert result["max_rules"] == 1048576, (
            f"Expected max_rules=1048576, got {result['max_rules']}"
        )
        assert result["number_tbl8s"] == 65536, (
            f"Expected number_tbl8s=65536, got {result['number_tbl8s']}"
        )
        assert result["memory_bytes"] > 0, "memory_bytes should be > 0"

    def test_fib_info_without_fib(self, no_fib_dpdk_process, no_fib_control_client):
        """Verify get_fib_info returns 0 rules when no FIB is configured."""
        response = no_fib_control_client.get_fib_info()
        assert response["status"] == "success", f"Expected success: {response}"

        result = response["result"]
        assert result["rules_count"] == 0, (
            f"Expected 0 rules, got {result['rules_count']}"
        )
        assert result["memory_bytes"] == 67108864, (
            f"Expected 67108864 memory_bytes, got {result['memory_bytes']}"
        )
