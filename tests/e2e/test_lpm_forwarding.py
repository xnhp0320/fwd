"""E2E tests for LPM forwarding processor.

Launches the DPDK application with the lpm_forwarding processor and a FIB
loaded from ipv4_test_fib.txt (10 prefixes).  Sends packets with
destination IPs from the FIB and verifies that:
  1. The process starts and loads the FIB without error.
  2. Packets are forwarded (received back on the TAP interface).
  3. Per-thread packet stats increase after traffic.
"""

import random
import subprocess
import threading
import time

import pytest
from pathlib import Path
from scapy.all import conf, Ether, IP, TCP, Raw, sendp, sniff

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
FIB_FILE = PROJECT_ROOT / "fib" / "ipv4_test_fib.txt"


def load_fib_ips(path: Path) -> list:
    """Read the FIB file and return all IPs."""
    ips = []
    with open(path) as f:
        while True:
            ip_line = f.readline()
            if not ip_line:
                break
            ip_line = ip_line.strip()
            cidr_line = f.readline()  # consume the CIDR line
            if not cidr_line:
                break
            if ip_line:
                ips.append(ip_line)
    return ips


def bring_interfaces_up(interfaces):
    """Bring TAP interfaces up and reload Scapy's interface cache."""
    for iface in interfaces:
        subprocess.run(
            ["sudo", "ip", "link", "set", iface, "up"],
            check=True,
        )
    time.sleep(1)
    conf.ifaces.reload()


# ---------------------------------------------------------------------------
# Config — single port, single thread, lpm_forwarding processor + fib_file
# ---------------------------------------------------------------------------

LPM_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "lpm_forwarding",
}


# ---------------------------------------------------------------------------
# Fixture override — inject fib_file into the generated JSON config
# ---------------------------------------------------------------------------

@pytest.fixture
def lpm_test_config(test_output_dir, request):
    """Generate config with fib_file field added."""
    import json
    from fixtures.config_generator import TestConfigGenerator

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

    # rte_lpm_create needs ~68MB+ for 1M rules; --no-huge defaults to 64MB.
    # Bump to 512MB so the LPM table fits in non-hugepage memory.
    config["additional_params"].append(["-m", "512"])

    config_path = test_output_dir / f"dpdk_lpm_{request.node.name}.json"
    TestConfigGenerator.write_config(config, str(config_path))
    return config_path


@pytest.fixture
def lpm_dpdk_process(binary_path, lpm_test_config, test_output_dir, request):
    """Launch DPDK with the LPM config (mirrors the standard dpdk_process fixture)."""
    from fixtures.dpdk_process import DpdkProcess

    process = DpdkProcess(
        binary_path=str(binary_path),
        config_path=str(lpm_test_config),
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
def lpm_control_client(lpm_dpdk_process):
    """Control client connected to the LPM DPDK process."""
    from fixtures.control_client import ControlClient

    client = ControlClient(socket_path="/tmp/dpdk_control.sock")
    if not client.connect(timeout=5.0, retry_count=10, retry_delay=0.5):
        pytest.fail("Failed to connect to control socket")
    yield client
    client.close()


@pytest.fixture
def lpm_tap_interfaces(lpm_test_config, lpm_dpdk_process):
    """Verify TAP interfaces created by the LPM DPDK process."""
    import json
    from fixtures.tap_interface import TapInterfaceManager

    with open(lpm_test_config) as f:
        config = json.load(f)

    interfaces = []
    for param_pair in config.get("additional_params", []):
        if len(param_pair) >= 2 and param_pair[0] == "--vdev" and "net_tap" in param_pair[1]:
            for part in param_pair[1].split(","):
                if part.startswith("iface="):
                    interfaces.append(part.split("=")[1])

    for iface in interfaces:
        if not TapInterfaceManager.wait_for_interface(iface, timeout=10):
            pytest.fail(f"TAP interface {iface} did not appear within timeout")

    yield interfaces


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestLpmForwarding:
    """E2E tests for the LPM forwarding processor."""

    def test_fib_loaded_and_process_ready(self, lpm_dpdk_process):
        """Verify the process starts, loads the FIB, and reaches ready state."""
        stdout = lpm_dpdk_process.get_stdout()
        assert "FIB loaded:" in stdout, (
            f"Expected 'FIB loaded:' in stdout, got:\n{stdout}"
        )
        assert lpm_dpdk_process.is_running()

    def test_packet_forwarded_with_fib_dst(
        self, lpm_dpdk_process, lpm_control_client, lpm_tap_interfaces
    ):
        """Send a packet whose dst_ip is in the FIB and verify it is forwarded."""
        bring_interfaces_up(lpm_tap_interfaces)
        assert lpm_dpdk_process.is_running()

        iface = lpm_tap_interfaces[0]
        dst_ips = load_fib_ips(FIB_FILE)
        dst_ip = dst_ips[0]

        pkt = (
            Ether(dst="ff:ff:ff:ff:ff:ff")
            / IP(src="192.168.99.1", dst=dst_ip)
            / TCP(sport=12345, dport=80)
            / Raw(load="LPM" * 10)
        )

        captured = []

        def do_sniff():
            pkts = sniff(
                iface=iface,
                filter=f"ip dst {dst_ip}",
                count=2,
                timeout=10,
            )
            captured.extend(pkts)

        t = threading.Thread(target=do_sniff)
        t.start()
        time.sleep(1)

        sendp(pkt, iface=iface, verbose=False)
        t.join(timeout=15)

        assert len(captured) > 0, (
            f"No packets captured on {iface} for dst {dst_ip}"
        )

    def test_stats_increase_after_traffic(
        self, lpm_dpdk_process, lpm_control_client, lpm_tap_interfaces
    ):
        """Send several packets with random FIB IPs and verify stats increase."""
        bring_interfaces_up(lpm_tap_interfaces)
        assert lpm_dpdk_process.is_running()

        iface = lpm_tap_interfaces[0]

        baseline = lpm_control_client.get_stats()
        assert baseline["status"] == "success"
        base_packets = baseline["result"]["total"]["packets"]

        dst_ips = load_fib_ips(FIB_FILE)
        for dst_ip in dst_ips:
            pkt = (
                Ether(dst="ff:ff:ff:ff:ff:ff")
                / IP(src="192.168.99.1", dst=dst_ip)
                / TCP(sport=random.randint(1024, 65535), dport=80)
                / Raw(load="X" * 30)
            )
            sendp(pkt, iface=iface, verbose=False)

        time.sleep(1)

        response = lpm_control_client.get_stats()
        assert response["status"] == "success"
        delta = response["result"]["total"]["packets"] - base_packets
        assert delta >= len(dst_ips), (
            f"Expected at least {len(dst_ips)} new packets, got {delta}"
        )
