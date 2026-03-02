"""E2E packet statistics tests for DPDK application.

This module tests packet forwarding and per-PMD-thread statistics including:
- Baseline stats verification (near-zero counters before traffic)
- Packet forwarding through TAP devices via Scapy
- Stats counter accuracy after known traffic
- Multi-thread stats aggregation (per-thread sum equals total)
"""

import pytest
import subprocess
import threading
import time
from scapy.all import conf, Ether, IP, Raw, sendp, sniff

# net_tap requires equal RX/TX queue counts; processor uses only the first TX queue.
STATS_CONFIG = {"num_ports": 1, "num_threads": 2, "num_rx_queues": 2, "num_tx_queues": 2}


@pytest.mark.parametrize("test_config", [STATS_CONFIG], indirect=True)
class TestPacketStats:
    """Test packet forwarding and per-PMD-thread statistics."""

    def _bring_interfaces_up(self, interfaces):
        """Bring TAP interfaces up and wait for them to settle."""
        for iface in interfaces:
            subprocess.run(
                ['sudo', 'ip', 'link', 'set', iface, 'up'],
                check=True,
            )
        # Allow time for the kernel to fully activate the interfaces
        # and for any initial solicitation packets to be processed.
        time.sleep(1)
        # Reload Scapy's interface cache so it picks up the (re-)created
        # TAP devices with their current interface indices.
        conf.ifaces.reload()

    def _send_packets(self, iface, count, size=84):
        """Craft and send Ethernet/IP packets into a TAP device."""
        payload_size = size - 14 - 20  # subtract Ether and IP headers
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IP(dst="10.0.0.1") / Raw(load="X" * payload_size)
        sendp(pkt, iface=iface, count=count, verbose=False)

    def _sniff_packets(self, iface, count, timeout=5):
        """Capture packets from a TAP device."""
        return sniff(iface=iface, count=count, timeout=timeout)

    def test_stats_baseline_zero(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Verify stats counters are near-zero before any traffic is sent.

        TAP interfaces may receive a small number of OS-generated packets
        (e.g. IPv6 Neighbor/Router Solicitation) when brought up, so we
        allow a small tolerance instead of asserting exactly zero.
        """
        response = control_client.get_stats()
        assert response['status'] == 'success', "get_stats should succeed"

        total = response['result']['total']
        assert total['packets'] <= 5, f"Expected near-zero packets, got {total['packets']}"

    def test_packet_forwarding(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send packets via Scapy and verify they are forwarded back."""
        self._bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IP(dst="10.0.0.1") / Raw(load="X" * 50)
        captured = []

        # Use a BPF filter to ignore OS-generated traffic (ARP, IPv6 NS/RS)
        # and only capture IP packets destined to our test address.
        def do_sniff():
            pkts = sniff(iface=iface, filter="ip dst 10.0.0.1",
                         count=2, timeout=10)
            captured.extend(pkts)

        t = threading.Thread(target=do_sniff)
        t.start()
        time.sleep(1)

        sendp(pkt, iface=iface, verbose=False)
        t.join(timeout=15)

        assert len(captured) > 0, f"No packets received on {iface} within timeout"

        found = any(
            p.haslayer(Raw) and b"X" * 50 in bytes(p[Raw])
            for p in captured
        )
        assert found, "No captured packet matched the sent payload"

    def test_stats_after_traffic(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send known traffic and verify stats counters increased."""
        self._bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]

        # Record baseline before sending
        baseline = control_client.get_stats()
        assert baseline['status'] == 'success'
        base_packets = baseline['result']['total']['packets']
        base_bytes = baseline['result']['total']['bytes']

        num_packets = 10
        packet_size = 84  # 14 Ether + 20 IP + 50 payload

        self._send_packets(iface, count=num_packets, size=packet_size)
        time.sleep(1)

        response = control_client.get_stats()
        assert response['status'] == 'success', "get_stats should succeed"

        total = response['result']['total']
        delta_packets = total['packets'] - base_packets
        delta_bytes = total['bytes'] - base_bytes

        # The forwarding processor loops packets back through the TAP,
        # so DPDK may see more than num_packets. Assert at least num_packets.
        assert delta_packets >= num_packets, \
            f"Expected at least {num_packets} new packets, got {delta_packets}"
        assert delta_bytes >= num_packets * packet_size, \
            f"Expected at least {num_packets * packet_size} new bytes, got {delta_bytes}"

    def test_multi_thread_stats_sum(self, dpdk_process, control_client, tap_interfaces, test_config):
        """With multiple PMD threads, verify per-thread sum equals total."""
        self._bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]

        self._send_packets(iface, count=20, size=84)
        time.sleep(1)

        response = control_client.get_stats()
        assert response['status'] == 'success', "get_stats should succeed"

        result = response['result']
        threads = result['threads']
        total = result['total']

        sum_packets = sum(t['packets'] for t in threads)
        sum_bytes = sum(t['bytes'] for t in threads)

        assert sum_packets == total['packets'], \
            f"Sum of per-thread packets ({sum_packets}) != total ({total['packets']})"
        assert sum_bytes == total['bytes'], \
            f"Sum of per-thread bytes ({sum_bytes}) != total ({total['bytes']})"
        assert total['packets'] >= 20, \
            f"Expected at least 20 packets total, got {total['packets']}"
