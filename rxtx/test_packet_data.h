// rxtx/test_packet_data.h
// Scapy-generated hex string constants for deterministic packet parser tests.
// Each constant is preceded by the scapy command that produces the packet.
// Checksums are zeroed (0x0000) — the parser does not validate data checksums.
// All multi-byte fields are big-endian (network byte order).

#ifndef RXTX_TEST_PACKET_DATA_H_
#define RXTX_TEST_PACKET_DATA_H_

namespace rxtx::testing {

// ============================================================================
// Valid packets
// ============================================================================

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/TCP(sport=1234,dport=80)).hex()
// Ethernet(14): dst=00:00:00:00:00:00, src=00:00:00:00:00:00, type=0x0800
// IPv4(20): ver=4, ihl=5, tos=0, total_len=40, id=1, flags=0, frag=0,
//           ttl=64, proto=6(TCP), cksum=0, src=10.0.0.1, dst=10.0.0.2
// TCP(20): sport=1234, dport=80, seq=0, ack=0, data_off=5, flags=S(0x02),
//          window=8192, cksum=0, urg=0
static constexpr const char* kIpv4TcpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/UDP(sport=1234,dport=53)/Raw(b"")).hex()
// Ethernet(14) + IPv4(20): total_len=28, proto=17(UDP)
// UDP(8): sport=1234, dport=53, len=8, cksum=0
static constexpr const char* kIpv4UdpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001c"          // total_len=28
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4011"          // ttl=64, proto=17 (UDP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0035"          // dport=53
    "0008"          // udp_len=8
    "0000";         // checksum (zeroed)

// scapy: bytes(Ether()/IPv6(src="::1",dst="::2")/TCP(sport=1234,dport=80)).hex()
// Ethernet(14): type=0x86dd
// IPv6(40): ver=6, tc=0, flow=0, payload_len=20, next_hdr=6(TCP), hop_limit=64
//           src=::1, dst=::2
// TCP(20): sport=1234, dport=80, data_off=5
static constexpr const char* kIpv6TcpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0014"          // payload_len=20
    "06"            // next_hdr=6 (TCP)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IPv6(src="::1",dst="::2")/UDP(sport=1234,dport=53)/Raw(b"")).hex()
// Ethernet(14): type=0x86dd
// IPv6(40): payload_len=8, next_hdr=17(UDP), hop_limit=64
// UDP(8): sport=1234, dport=53, len=8
static constexpr const char* kIpv6UdpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0008"          // payload_len=8
    "11"            // next_hdr=17 (UDP)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "04d2"          // sport=1234
    "0035"          // dport=53
    "0008"          // udp_len=8
    "0000";         // checksum (zeroed)

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2",flags="MF",frag=0)/TCP(sport=1234,dport=80)).hex()
// IPv4: flags=MF (0x2000 in frag field), frag_offset=0, proto=6(TCP)
// First fragment — L4 header is present and should be parsed normally.
static constexpr const char* kIpv4FragFirstPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "2000"          // flags=MF(0x2000), frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2",proto=6,frag=185)/Raw(b"\x00"*20)).hex()
// IPv4: frag_offset=185 (in 8-byte units), proto=6(TCP)
// Non-first fragment — no L4 header, ports should be zeroed.
// Raw 20 bytes of zeros follow the IP header (simulating fragment payload).
static constexpr const char* kIpv4FragSecondPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40 (20 IP + 20 raw)
    "0001"          // id=1
    "00b9"          // flags=0, frag_offset=185
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "0000000000000000000000000000000000000000";  // 20 bytes raw payload

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2",ihl=6,options=[IPOption(b"\x01\x00\x00\x00")])/TCP(sport=1234,dport=80)).hex()
// IPv4: ihl=6 (24-byte header), 4 bytes of options (NOP padding).
// TCP follows at offset 24 from IP start.
static constexpr const char* kIpv4OptionsPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4600"          // ver=4, ihl=6, tos=0
    "002c"          // total_len=44 (24 IP + 20 TCP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "01000000"      // IP options: 4 bytes (NOP + padding)
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IPv6(src="::1",dst="::2")/IPv6ExtHdrFragment(offset=0,m=1,id=0x1234)/TCP(sport=1234,dport=80)).hex()
// IPv6: next_hdr=44 (Fragment)
// Fragment header(8): next_hdr=6(TCP), reserved=0, offset=0, M=1, id=0x1234
// First fragment — L4 should be parsed.
static constexpr const char* kIpv6FragmentPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "001c"          // payload_len=28 (8 frag + 20 TCP)
    "2c"            // next_hdr=44 (Fragment)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "06"            // frag next_hdr=6 (TCP)
    "00"            // frag reserved=0
    "0001"          // frag offset=0, res=0, M=1 (offset_field: 0<<3 | 1 = 0x0001)
    "00001234"      // frag id=0x1234
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IPv6(src="::1",dst="::2")/IPv6ExtHdrHopByHop()/IPv6ExtHdrRouting()/TCP(sport=1234,dport=80)).hex()
// IPv6: next_hdr=0 (Hop-by-Hop)
// Hop-by-Hop(8): next_hdr=43(Routing), len=0 (8 bytes total)
// Routing(8): next_hdr=6(TCP), len=0 (8 bytes total)
// l3_len = 40 + 8 + 8 = 56
static constexpr const char* kIpv6ExtHdrPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0024"          // payload_len=36 (8 HBH + 8 Routing + 20 TCP)
    "00"            // next_hdr=0 (Hop-by-Hop)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "2b"            // HBH next_hdr=43 (Routing)
    "00"            // HBH len=0 (8 bytes total)
    "000000000000"  // HBH padding (6 bytes to fill 8-byte header)
    "06"            // Routing next_hdr=6 (TCP)
    "00"            // Routing len=0 (8 bytes total)
    "00"            // Routing type=0
    "00"            // Segments left=0
    "00000000"      // Routing reserved (4 bytes to fill 8-byte header)
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/Dot1Q(vlan=100)/IP(src="10.0.0.1",dst="10.0.0.2")/TCP(sport=1234,dport=80)).hex()
// Ethernet(14): type=0x8100 (VLAN)
// VLAN(4): vlan_id=100, type=0x0800
// l2_len = 18
static constexpr const char* kVlanIpv4TcpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "8100"          // EtherType: 802.1Q VLAN
    "0064"          // VLAN TCI: pri=0, dei=0, vlan_id=100
    "0800"          // inner EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether(type=0x88a8)/Dot1Q(vlan=200,type=0x8100)/Dot1Q(vlan=300)/IPv6(src="::1",dst="::2")/UDP(sport=1234,dport=53)/Raw(b"")).hex()
// Ethernet(14): type=0x88a8 (QinQ)
// Outer VLAN(4): vlan_id=200, type=0x8100
// Inner VLAN(4): vlan_id=300, type=0x86dd
// l2_len = 22
static constexpr const char* kQinQIpv6UdpPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "88a8"          // EtherType: 802.1ad QinQ
    "00c8"          // Outer VLAN TCI: pri=0, dei=0, vlan_id=200
    "8100"          // inner EtherType: 802.1Q
    "012c"          // Inner VLAN TCI: pri=0, dei=0, vlan_id=300
    "86dd"          // inner EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0008"          // payload_len=8
    "11"            // next_hdr=17 (UDP)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "04d2"          // sport=1234
    "0035"          // dport=53
    "0008"          // udp_len=8
    "0000";         // checksum (zeroed)

// scapy: bytes(Ether()/IP(src="192.168.1.1",dst="192.168.1.2")/UDP(sport=49152,dport=4789)/VXLAN(vni=42)/Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/TCP(sport=1234,dport=80)).hex()
// Outer: Ethernet(14) + IPv4(20) + UDP(8) + VXLAN(8) = 50 bytes
// Inner: Ethernet(14) + IPv4(20) + TCP(20) = 54 bytes
// Outer IPv4: total_len = 20 + 8 + 8 + 54 = 90, proto=17(UDP)
// Outer UDP: sport=49152, dport=4789, len = 8 + 8 + 54 = 70
// VXLAN: flags=0x08 (VNI valid), VNI=42
static constexpr const char* kVxlanIpv4TcpPacket =
    // --- Outer Ethernet ---
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    // --- Outer IPv4 ---
    "4500"          // ver=4, ihl=5, tos=0
    "005a"          // total_len=90
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4011"          // ttl=64, proto=17 (UDP)
    "0000"          // header checksum (zeroed)
    "c0a80101"      // src=192.168.1.1
    "c0a80102"      // dst=192.168.1.2
    // --- Outer UDP ---
    "c000"          // sport=49152
    "12b5"          // dport=4789
    "0046"          // udp_len=70
    "0000"          // checksum (zeroed)
    // --- VXLAN ---
    "08"            // flags=0x08 (VNI valid)
    "000000"        // reserved
    "00002a"        // VNI=42
    "00"            // reserved
    // --- Inner Ethernet ---
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    // --- Inner IPv4 ---
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    // --- Inner TCP ---
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/ICMP(type=8,code=0,id=0x1234)).hex()
// IPv4: proto=1 (ICMP)
// ICMP(8): type=8(Echo Request), code=0, cksum=0, id=0x1234, seq=0
// Expected: dst_port=(8<<8)|0=2048, src_port=0x1234
static constexpr const char* kIcmpEchoRequestPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001c"          // total_len=28 (20 IP + 8 ICMP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4001"          // ttl=64, proto=1 (ICMP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "08"            // ICMP type=8 (Echo Request)
    "00"            // ICMP code=0
    "0000"          // ICMP checksum (zeroed)
    "1234"          // ICMP id=0x1234
    "0000";         // ICMP seq=0

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/ICMP(type=0,code=0,id=0x5678)).hex()
// ICMP: type=0(Echo Reply), code=0, id=0x5678
// Expected: dst_port=0, src_port=0x5678
static constexpr const char* kIcmpEchoReplyPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001c"          // total_len=28 (20 IP + 8 ICMP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4001"          // ttl=64, proto=1 (ICMP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "00"            // ICMP type=0 (Echo Reply)
    "00"            // ICMP code=0
    "0000"          // ICMP checksum (zeroed)
    "5678"          // ICMP id=0x5678
    "0000";         // ICMP seq=0

// scapy: bytes(Ether()/IPv6(src="::1",dst="::2")/ICMPv6EchoRequest(id=0xabcd)).hex()
// IPv6: next_hdr=58 (ICMPv6)
// ICMPv6(8): type=128(Echo Request), code=0, cksum=0, id=0xabcd, seq=0
// Expected: dst_port=(128<<8)|0=32768, src_port=0xabcd
static constexpr const char* kIcmpv6EchoRequestPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0008"          // payload_len=8
    "3a"            // next_hdr=58 (ICMPv6)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "80"            // ICMPv6 type=128 (Echo Request)
    "00"            // ICMPv6 code=0
    "0000"          // ICMPv6 checksum (zeroed)
    "abcd"          // ICMPv6 id=0xabcd
    "0000";         // ICMPv6 seq=0

// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/ICMP(type=3,code=1)).hex()
// ICMP: type=3(Dest Unreachable), code=1(Host Unreachable)
// Expected: dst_port=(3<<8)|1=769, src_port=0 (non-echo)
static constexpr const char* kIcmpDestUnreachPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001c"          // total_len=28 (20 IP + 8 ICMP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4001"          // ttl=64, proto=1 (ICMP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "03"            // ICMP type=3 (Dest Unreachable)
    "01"            // ICMP code=1 (Host Unreachable)
    "0000"          // ICMP checksum (zeroed)
    "0000"          // unused (not echo, so no id)
    "0000";         // unused

// ============================================================================
// Malformed packets
// ============================================================================

// Malformation: fewer than 14 bytes (only 6 bytes — just a dst MAC).
// Expected: ParseResult::kTooShort
static constexpr const char* kTruncatedEthernetPacket =
    "ffffffffffff";  // 6 bytes — incomplete Ethernet header

// Malformation: valid 14-byte Ethernet header + only 10 bytes of IPv4
// (minimum IPv4 header is 20 bytes).
// Expected: ParseResult::kTooShort
static constexpr const char* kTruncatedIpv4Packet =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "45000028000100004006";  // 10 bytes of IPv4 (truncated before addresses)

// Malformation: valid Ethernet + valid IPv4 (proto=TCP) but only 10 bytes
// of TCP (minimum TCP header is 20 bytes).
// Expected: ParseResult::kTooShort
static constexpr const char* kTruncatedL4Packet =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001e"          // total_len=30 (20 IP + 10 TCP — but we only provide 10 bytes of TCP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d20050000000000000";  // 10 bytes of TCP (truncated)

// Malformation: valid Ethernet + IPv4 with total_length=100 but actual
// data after Ethernet is only 40 bytes (20 IP + 20 TCP).
// Expected: ParseResult::kLengthMismatch
static constexpr const char* kIpv4TotalLenExceedsPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0064"          // total_len=100 (EXCEEDS actual data)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// Malformation: valid Ethernet + IPv6 with payload_len=100 but actual
// payload is only 20 bytes (TCP header).
// Expected: ParseResult::kLengthMismatch
static constexpr const char* kIpv6PayloadLenExceedsPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "86dd"          // EtherType: IPv6
    "60000000"      // ver=6, tc=0, flow_label=0
    "0064"          // payload_len=100 (EXCEEDS actual data)
    "06"            // next_hdr=6 (TCP)
    "40"            // hop_limit=64
    "00000000000000000000000000000001"  // src=::1
    "00000000000000000000000000000002"  // dst=::2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// Malformation: valid Ethernet + IPv4 with IHL=3 (less than minimum 5).
// version_ihl byte = (4 << 4) | 3 = 0x43
// Expected: ParseResult::kMalformedHeader
static constexpr const char* kIpv4BadIhlPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4300"          // ver=4, ihl=3 (BAD), tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// Malformation: valid Ethernet (type=0x0800) + IP header with version=7.
// version_ihl byte = (7 << 4) | 5 = 0x75
// Parser checks IHL first (5 >= 5, passes), then version (7 != 4, fails).
// Expected: ParseResult::kUnsupportedVersion
static constexpr const char* kBadIpVersionPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "7500"          // ver=7 (BAD), ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

// Malformation: valid Ethernet + IPv4 + UDP with dgram_len=100 but
// IPv4 total_length says only 28 bytes (20 IP + 8 UDP), so expected
// UDP length is 8, not 100.
// Expected: ParseResult::kUdpLengthMismatch
static constexpr const char* kUdpLenMismatchPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "001c"          // total_len=28 (20 IP + 8 UDP)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4011"          // ttl=64, proto=17 (UDP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0035"          // dport=53
    "0064"          // udp_len=100 (MISMATCH: should be 8)
    "0000";         // checksum (zeroed)

// Malformation: valid outer Ethernet + IPv4 + UDP(dport=4789) but
// truncated VXLAN header (only 4 bytes instead of 8).
// The parser detects VXLAN via UDP dport=4789, then tries to read
// the 8-byte VXLAN header but data is too short.
// Expected: ParseResult::kTooShort
static constexpr const char* kVxlanTruncatedPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0020"          // total_len=32 (20 IP + 8 UDP + 4 truncated VXLAN)
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4011"          // ttl=64, proto=17 (UDP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "c000"          // sport=49152
    "12b5"          // dport=4789 (VXLAN)
    "000c"          // udp_len=12 (8 UDP + 4 truncated VXLAN)
    "0000"          // checksum (zeroed)
    "08000000";     // 4 bytes of VXLAN (truncated — need 8)

// Note: This is a normal valid packet. The test should pass ol_flags with
// RTE_MBUF_F_RX_IP_CKSUM_BAD to the HexPacket constructor to simulate
// a hardware-detected checksum error.
// Expected: ParseResult::kChecksumError (when ol_flags is set)
// scapy: bytes(Ether()/IP(src="10.0.0.1",dst="10.0.0.2")/TCP(sport=1234,dport=80)).hex()
static constexpr const char* kBadChecksumPacket =
    "000000000000"  // dst MAC
    "000000000000"  // src MAC
    "0800"          // EtherType: IPv4
    "4500"          // ver=4, ihl=5, tos=0
    "0028"          // total_len=40
    "0001"          // id=1
    "0000"          // flags=0, frag_offset=0
    "4006"          // ttl=64, proto=6 (TCP)
    "0000"          // header checksum (zeroed)
    "0a000001"      // src=10.0.0.1
    "0a000002"      // dst=10.0.0.2
    "04d2"          // sport=1234
    "0050"          // dport=80
    "00000000"      // seq=0
    "00000000"      // ack=0
    "5002"          // data_off=5, flags=SYN
    "2000"          // window=8192
    "0000"          // checksum (zeroed)
    "0000";         // urgent pointer=0

}  // namespace rxtx::testing

#endif  // RXTX_TEST_PACKET_DATA_H_
