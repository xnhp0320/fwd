"""E2E five-tuple forwarding tests for DPDK application.

Tests verify that the FiveTupleForwardingProcessor correctly parses packets,
inserts flow entries into the FastLookupTable, and that the get_flow_table
control command returns the expected entries.
"""

import pytest
import subprocess
import time
from scapy.all import conf, Ether, IP, IPv6, TCP, Raw, sendp

# Configuration for five-tuple forwarding tests.
# net_tap requires equal RX/TX queue counts.
FIVE_TUPLE_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"},
}


def bring_interfaces_up(interfaces):
    """Bring TAP interfaces up and reload Scapy's interface cache."""
    for iface in interfaces:
        subprocess.run(
            ['sudo', 'ip', 'link', 'set', iface, 'up'],
            check=True,
        )
    time.sleep(1)
    conf.ifaces.reload()


def collect_flow_entries(response):
    """Collect all flow entries from a get_flow_table response across all threads.

    Args:
        response: Parsed JSON response from get_flow_table command

    Returns:
        List of entry dicts from all threads
    """
    entries = []
    for thread in response.get('result', {}).get('threads', []):
        entries.extend(thread.get('entries', []))
    return entries


def send_packet(iface, src_ip, dst_ip, src_port, dst_port, ipv6=False):
    """Send a single TCP packet with the given five-tuple via scapy.

    Args:
        iface: TAP interface name
        src_ip: Source IP address string
        dst_ip: Destination IP address string
        src_port: Source TCP port
        dst_port: Destination TCP port
        ipv6: If True, send IPv6 packet; otherwise IPv4
    """
    if ipv6:
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IPv6(src=src_ip, dst=dst_ip) / TCP(sport=src_port, dport=dst_port) / Raw(load="X" * 20)
    else:
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IP(src=src_ip, dst=dst_ip) / TCP(sport=src_port, dport=dst_port) / Raw(load="X" * 20)
    sendp(pkt, iface=iface, verbose=False)


@pytest.mark.parametrize("test_config", [FIVE_TUPLE_CONFIG], indirect=True)
class TestFiveTupleForwarding:
    """Test five-tuple forwarding processor via get_flow_table command."""

    def test_flow_table_response_structure(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Verify get_flow_table response has the expected JSON structure."""
        response = control_client.get_flow_table()

        assert response['status'] == 'success', f"Expected success, got: {response}"
        assert 'result' in response, "Response should have 'result' field"

        result = response['result']
        assert 'threads' in result, "Result should have 'threads' field"

        threads = result['threads']
        assert isinstance(threads, list), "threads should be a list"
        assert len(threads) > 0, "Should have at least one thread"

        for thread in threads:
            assert 'lcore_id' in thread, "Thread should have 'lcore_id'"
            assert isinstance(thread['lcore_id'], int), "lcore_id should be int"
            assert 'entries' in thread, "Thread should have 'entries'"
            assert isinstance(thread['entries'], list), "entries should be a list"

    def test_single_ipv4_flow_entry(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a single IPv4/TCP packet and verify the flow table entry.

        Validates: Requirements 5.1, 5.2, 5.3
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "192.168.1.100"
        dst_ip = "10.0.0.50"
        src_port = 12345
        dst_port = 80

        send_packet(iface, src_ip, dst_ip, src_port, dst_port)
        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_flow_table()
        assert response['status'] == 'success', f"get_flow_table failed: {response}"

        entries = collect_flow_entries(response)

        # Find the entry matching our sent packet
        matching = [e for e in entries
                    if e['src_ip'] == src_ip
                    and e['dst_ip'] == dst_ip
                    and e['src_port'] == src_port
                    and e['dst_port'] == dst_port]

        assert len(matching) >= 1, \
            f"Expected entry for {src_ip}:{src_port} -> {dst_ip}:{dst_port}, got entries: {entries}"

        entry = matching[0]
        assert entry['is_ipv6'] == False, "IPv4 packet should have is_ipv6=false"
        assert entry['protocol'] == 6, f"TCP protocol should be 6, got {entry['protocol']}"

    def test_multiple_distinct_flows(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send multiple packets with distinct five-tuples and verify distinct entries.

        Validates: Requirements 6.1, 6.2
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]

        # Define 3 distinct five-tuples
        flows = [
            ("192.168.1.1", "10.0.0.1", 1001, 80),
            ("192.168.1.2", "10.0.0.2", 1002, 443),
            ("192.168.1.3", "10.0.0.3", 1003, 8080),
        ]

        for src_ip, dst_ip, src_port, dst_port in flows:
            send_packet(iface, src_ip, dst_ip, src_port, dst_port)

        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_flow_table()
        assert response['status'] == 'success', f"get_flow_table failed: {response}"

        entries = collect_flow_entries(response)

        # Verify each flow has a matching entry
        for src_ip, dst_ip, src_port, dst_port in flows:
            matching = [e for e in entries
                        if e['src_ip'] == src_ip
                        and e['dst_ip'] == dst_ip
                        and e['src_port'] == src_port
                        and e['dst_port'] == dst_port]
            assert len(matching) >= 1, \
                f"Missing entry for {src_ip}:{src_port} -> {dst_ip}:{dst_port}"

        # Verify total entry count is at least the number of distinct flows sent
        # (may be more due to forwarded packets being re-ingested)
        assert len(entries) >= len(flows), \
            f"Expected at least {len(flows)} entries, got {len(entries)}"

    def test_duplicate_packets_single_entry(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send duplicate packets and verify only one flow table entry.

        Validates: Requirements 7.1, 7.2
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "172.16.0.1"
        dst_ip = "172.16.0.2"
        src_port = 5000
        dst_port = 6000

        # Send the same five-tuple 3 times
        for _ in range(3):
            send_packet(iface, src_ip, dst_ip, src_port, dst_port)

        time.sleep(1)

        response = control_client.get_flow_table()
        assert response['status'] == 'success', f"get_flow_table failed: {response}"

        entries = collect_flow_entries(response)

        matching = [e for e in entries
                    if e['src_ip'] == src_ip
                    and e['dst_ip'] == dst_ip
                    and e['src_port'] == src_port
                    and e['dst_port'] == dst_port]

        assert len(matching) == 1, \
            f"Expected exactly 1 entry for duplicate five-tuple, got {len(matching)}: {matching}"

    def test_ipv6_flow_entry(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a single IPv6/TCP packet and verify the flow table entry.

        Validates: Requirement 8.1
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "fd00::1"
        dst_ip = "fd00::2"
        src_port = 9000
        dst_port = 9001

        send_packet(iface, src_ip, dst_ip, src_port, dst_port, ipv6=True)
        time.sleep(1)

        response = control_client.get_flow_table()
        assert response['status'] == 'success', f"get_flow_table failed: {response}"

        entries = collect_flow_entries(response)

        # Find the entry matching our sent IPv6 packet
        matching = [e for e in entries
                    if e.get('is_ipv6') == True
                    and e['src_port'] == src_port
                    and e['dst_port'] == dst_port]

        assert len(matching) >= 1, \
            f"Expected IPv6 entry for [{src_ip}]:{src_port} -> [{dst_ip}]:{dst_port}, got entries: {entries}"

        entry = matching[0]
        assert entry['is_ipv6'] == True, "IPv6 packet should have is_ipv6=true"
        assert entry['protocol'] == 6, f"TCP protocol should be 6, got {entry['protocol']}"




