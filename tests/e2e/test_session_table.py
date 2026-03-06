"""E2E session table tests for DPDK application.

Tests verify that the session table correctly tracks sessions when packets
are forwarded through the five_tuple_forwarding processor, and that the
get_sessions control command returns the expected session entries.
"""

import pytest
import subprocess
import time
from scapy.all import conf, Ether, IP, IPv6, TCP, UDP, Raw, sendp

# Configuration for session table tests.
# Uses five_tuple_forwarding processor with session tracking enabled.
SESSION_CONFIG = {
    "num_ports": 1,
    "num_threads": 1,
    "num_rx_queues": 1,
    "num_tx_queues": 1,
    "processor_name": "five_tuple_forwarding",
    "processor_params": {"capacity": "1024"},
    "session_capacity": 1024,
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


def send_packet(iface, src_ip, dst_ip, src_port, dst_port, protocol="tcp", ipv6=False):
    """Send a single packet with the given five-tuple via scapy.

    Args:
        iface: TAP interface name
        src_ip: Source IP address string
        dst_ip: Destination IP address string
        src_port: Source port
        dst_port: Destination port
        protocol: "tcp" or "udp" (default: "tcp")
        ipv6: If True, send IPv6 packet; otherwise IPv4
    """
    transport = TCP(sport=src_port, dport=dst_port) if protocol == "tcp" else UDP(sport=src_port, dport=dst_port)

    if ipv6:
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IPv6(src=src_ip, dst=dst_ip) / transport / Raw(load="X" * 20)
    else:
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IP(src=src_ip, dst=dst_ip) / transport / Raw(load="X" * 20)

    sendp(pkt, iface=iface, verbose=False)


def collect_sessions(response):
    """Extract sessions list from a get_sessions response.

    Args:
        response: Parsed JSON response from get_sessions command

    Returns:
        List of session dicts
    """
    return response.get('result', {}).get('sessions', [])


@pytest.mark.parametrize("test_config", [SESSION_CONFIG], indirect=True)
class TestSessionTable:
    """Test session table behavior via get_sessions command."""

    def test_empty_session_table(self, dpdk_process, control_client):
        """Verify get_sessions returns success with at most a few OS-generated sessions.

        TAP interfaces may receive OS-generated packets (e.g. IPv6 Neighbor/Router
        Solicitation) when brought up, so we allow a small tolerance.

        Validates: Requirements 6.1
        """
        response = control_client.get_sessions()

        assert response['status'] == 'success', f"Expected success, got: {response}"
        sessions = collect_sessions(response)
        assert len(sessions) <= 10, \
            f"Expected at most a few OS-generated sessions, got {len(sessions)}: {sessions}"

    def test_single_ipv4_session(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a single IPv4/TCP packet and verify the session entry.

        Validates: Requirements 4.1
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

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find the session matching our sent packet
        matching = [s for s in sessions
                    if s['src_ip'] == src_ip
                    and s['dst_ip'] == dst_ip
                    and s['src_port'] == src_port
                    and s['dst_port'] == dst_port]

        assert len(matching) >= 1, \
            f"Expected session for {src_ip}:{src_port} -> {dst_ip}:{dst_port}, got sessions: {sessions}"

        session = matching[0]
        assert session['is_ipv6'] == False, "IPv4 packet should have is_ipv6=false"
        assert session['protocol'] == 6, f"TCP protocol should be 6, got {session['protocol']}"

    def test_multiple_distinct_sessions(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send 3 packets with distinct five-tuples and verify 3 separate session entries.

        Validates: Requirements 4.2
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "192.168.1.100"
        dst_ip = "10.0.0.50"
        dst_port = 80

        # Send 3 packets with distinct src_port values to create distinct five-tuples
        src_ports = [11111, 22222, 33333]
        for port in src_ports:
            send_packet(iface, src_ip, dst_ip, port, dst_port)

        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Verify each distinct five-tuple produced a separate session entry
        for port in src_ports:
            matching = [s for s in sessions
                        if s['src_ip'] == src_ip
                        and s['dst_ip'] == dst_ip
                        and s['src_port'] == port
                        and s['dst_port'] == dst_port]
            assert len(matching) >= 1, \
                f"Expected session for src_port={port}, got sessions: {sessions}"

        assert len(sessions) >= 3, \
            f"Expected at least 3 distinct sessions, got {len(sessions)}: {sessions}"

    def test_duplicate_packets_single_session(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send the same five-tuple 3 times and verify exactly 1 session entry.

        Validates: Requirements 4.3
        """
        bring_interfaces_up(["dtap0"])
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "192.168.1.200"
        dst_ip = "10.0.0.100"
        src_port = 44444
        dst_port = 8080

        # Send the same five-tuple 3 times
        for _ in range(3):
            send_packet(iface, src_ip, dst_ip, src_port, dst_port)

        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find sessions matching our five-tuple
        matching = [s for s in sessions
                    if s['src_ip'] == src_ip
                    and s['dst_ip'] == dst_ip
                    and s['src_port'] == src_port
                    and s['dst_port'] == dst_port]

        assert len(matching) == 1, \
            f"Expected exactly 1 session for duplicate five-tuple, got {len(matching)}: {matching}"

    def test_session_protocol_field(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send TCP and UDP packets and verify the protocol field in each session entry.

        Validates: Requirements 5.1
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]

        # Use distinct five-tuples for TCP and UDP so they create separate sessions
        tcp_src_ip = "192.168.2.10"
        tcp_dst_ip = "10.0.1.10"
        tcp_src_port = 50001
        tcp_dst_port = 443

        udp_src_ip = "192.168.2.20"
        udp_dst_ip = "10.0.1.20"
        udp_src_port = 50002
        udp_dst_port = 53

        send_packet(iface, tcp_src_ip, tcp_dst_ip, tcp_src_port, tcp_dst_port, protocol="tcp")
        send_packet(iface, udp_src_ip, udp_dst_ip, udp_src_port, udp_dst_port, protocol="udp")
        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find TCP session and verify protocol=6
        tcp_matching = [s for s in sessions
                        if s['src_ip'] == tcp_src_ip
                        and s['dst_ip'] == tcp_dst_ip
                        and s['src_port'] == tcp_src_port
                        and s['dst_port'] == tcp_dst_port]
        assert len(tcp_matching) >= 1, \
            f"Expected TCP session for {tcp_src_ip}:{tcp_src_port} -> {tcp_dst_ip}:{tcp_dst_port}, got: {sessions}"
        assert tcp_matching[0]['protocol'] == 6, \
            f"TCP session protocol should be 6, got {tcp_matching[0]['protocol']}"

        # Find UDP session and verify protocol=17
        udp_matching = [s for s in sessions
                        if s['src_ip'] == udp_src_ip
                        and s['dst_ip'] == udp_dst_ip
                        and s['src_port'] == udp_src_port
                        and s['dst_port'] == udp_dst_port]
        assert len(udp_matching) >= 1, \
            f"Expected UDP session for {udp_src_ip}:{udp_src_port} -> {udp_dst_ip}:{udp_dst_port}, got: {sessions}"
        assert udp_matching[0]['protocol'] == 17, \
            f"UDP session protocol should be 17, got {udp_matching[0]['protocol']}"

    def test_session_version_field(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a packet, retrieve the session entry, and verify version > 0.

        Validates: Requirements 5.2
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "192.168.3.10"
        dst_ip = "10.0.2.10"
        src_port = 60001
        dst_port = 9090

        send_packet(iface, src_ip, dst_ip, src_port, dst_port)
        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find the session matching our sent packet
        matching = [s for s in sessions
                    if s['src_ip'] == src_ip
                    and s['dst_ip'] == dst_ip
                    and s['src_port'] == src_port
                    and s['dst_port'] == dst_port]

        assert len(matching) >= 1, \
            f"Expected session for {src_ip}:{src_port} -> {dst_ip}:{dst_port}, got sessions: {sessions}"

        session = matching[0]
        assert session['version'] > 0, \
            f"Session version should be > 0 (indicating entry was initialized), got {session['version']}"

    def test_session_timestamp_field(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a packet, retrieve the session entry, and verify timestamp > 0.

        Validates: Requirements 5.3
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "192.168.3.20"
        dst_ip = "10.0.2.20"
        src_port = 60002
        dst_port = 9091

        send_packet(iface, src_ip, dst_ip, src_port, dst_port)
        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find the session matching our sent packet
        matching = [s for s in sessions
                    if s['src_ip'] == src_ip
                    and s['dst_ip'] == dst_ip
                    and s['src_port'] == src_port
                    and s['dst_port'] == dst_port]

        assert len(matching) >= 1, \
            f"Expected session for {src_ip}:{src_port} -> {dst_ip}:{dst_port}, got sessions: {sessions}"

        session = matching[0]
        assert session['timestamp'] > 0, \
            f"Session timestamp should be > 0 (indicating entry timestamp was set), got {session['timestamp']}"

    def test_ipv6_session(self, dpdk_process, control_client, tap_interfaces, test_config):
        """Send a single IPv6/TCP packet and verify the session entry has is_ipv6=true.

        Validates: Requirements 7.1
        """
        bring_interfaces_up(tap_interfaces)
        assert dpdk_process.is_running(), "DPDK process died after bringing interfaces up"

        iface = tap_interfaces[0]
        src_ip = "fd00::1"
        dst_ip = "fd00::2"
        src_port = 55555
        dst_port = 443

        send_packet(iface, src_ip, dst_ip, src_port, dst_port, protocol="tcp", ipv6=True)
        time.sleep(1)  # Allow time for packet processing

        response = control_client.get_sessions()
        assert response['status'] == 'success', f"get_sessions failed: {response}"

        sessions = collect_sessions(response)

        # Find the IPv6 session matching our sent packet by port and protocol
        matching = [s for s in sessions
                    if s['src_port'] == src_port
                    and s['dst_port'] == dst_port
                    and s['protocol'] == 6]

        assert len(matching) >= 1, \
            f"Expected IPv6 session for src_port={src_port}, dst_port={dst_port}, protocol=6, got sessions: {sessions}"

        session = matching[0]
        assert session['is_ipv6'] == True, \
            f"IPv6 packet should have is_ipv6=true, got {session['is_ipv6']}"
        assert session['src_port'] == src_port, \
            f"Expected src_port={src_port}, got {session['src_port']}"
        assert session['dst_port'] == dst_port, \
            f"Expected dst_port={dst_port}, got {session['dst_port']}"
        assert session['protocol'] == 6, \
            f"TCP protocol should be 6, got {session['protocol']}"






