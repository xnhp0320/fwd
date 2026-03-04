# Requirements Document

## Introduction

This feature enhances the `PacketMetadata` parser and replaces the `TestPacketBuilder` with a scapy-based hex-string approach. The parser enhancements add support for IPv4 fragmentation detection, IPv4 options awareness, IPv6 extension header chain walking (including the IPv6 Fragment header), and explicit QinQ (double VLAN) metadata extraction. The test infrastructure rework replaces the manual fluent builder (`TestPacketBuilder`) with a lightweight `HexPacket` class that loads pre-generated hex strings (produced offline by scapy) into DPDK mbufs, making test packets trivially reproducible and new test cases easy to add.

## Glossary

- **Parser**: The `PacketMetadata::Parse` static function in `rxtx/packet_metadata.cc` that inspects raw packet data and populates a `PacketMetadata` instance plus mbuf layer-length fields.
- **PacketMetadata**: The struct in `rxtx/packet_metadata.h` that stores parsed 5-tuple, VNI, and flags extracted from a packet.
- **Flags_Field**: The 64-bit `flags` member of `PacketMetadata`. Bit 0 = IPv6 indicator. Bits 1–63 are available for new per-packet flags introduced by this feature.
- **Fragment_Offset**: The 13-bit fragment offset field in the IPv4 header (or the IPv6 Fragment extension header), measured in 8-byte units.
- **MF_Flag**: The "More Fragments" bit in the IPv4 flags field (or the IPv6 Fragment extension header), indicating additional fragments follow.
- **IPv4_Options**: Variable-length option bytes between the fixed 20-byte IPv4 header and the L4 header, present when IHL > 5.
- **IPv6_Extension_Header**: A variable-length header chained via the Next Header field in IPv6, including Hop-by-Hop Options (0), Routing (43), Fragment (44), Destination Options (60), Authentication Header (51), and Encapsulating Security Payload (50).
- **IPv6_Fragment_Header**: The 8-byte IPv6 extension header (Next Header = 44) that carries fragment offset, MF flag, and identification fields.
- **QinQ**: Double VLAN tagging where an outer 802.1ad tag (EtherType 0x88A8) is followed by an inner 802.1Q tag (EtherType 0x8100).
- **VLAN_Info**: Parsed VLAN metadata including tag count (0, 1, or 2), outer VLAN ID, and inner VLAN ID.
- **HexPacket**: A new test utility class that converts a hex-encoded string into raw bytes and loads them into a DPDK mbuf for use in unit tests.
- **TestMbufAllocator**: The existing test allocator in `rxtx/test_utils.h` that creates DPDK mbufs from a mempool in `--no-huge` mode.
- **TestPacketBuilder**: The existing fluent builder class in `rxtx/test_packet_builder.h` that manually constructs packet bytes in mbuf memory (to be replaced).
- **Scapy**: A Python packet manipulation library used offline to construct packet headers and export them as hex strings for test input.
- **ICMP**: Internet Control Message Protocol, protocol number 1 for IPv4 and protocol number 58 (ICMPv6) for IPv6. Used for network diagnostics (ping), error reporting, and other control messages.
- **ICMP_Header**: The 8-byte ICMP header consisting of Type (1 byte), Code (1 byte), Checksum (2 bytes), and a 4-byte field whose interpretation depends on the Type (for Echo Request/Reply: Identifier (2 bytes) + Sequence Number (2 bytes)).
- **ICMP_Echo**: ICMP messages of Type 8 (Echo Request) or Type 0 (Echo Reply) for IPv4, and Type 128 (Echo Request) or Type 129 (Echo Reply) for IPv6. These messages carry an Identifier field used to match requests with replies.

## Requirements

### Requirement 1: IPv4 Fragmentation Detection

**User Story:** As a packet processor developer, I want the parser to detect IPv4 fragmented packets, so that downstream stages can handle fragments differently from complete datagrams (e.g., skip L4 parsing on non-first fragments).

#### Acceptance Criteria

1. WHEN a valid IPv4 packet has a non-zero Fragment_Offset or the MF_Flag is set, THE Parser SHALL set a fragmentation flag in the Flags_Field of PacketMetadata.
2. WHEN a valid IPv4 packet is a non-first fragment (Fragment_Offset > 0), THE Parser SHALL set source and destination L4 ports to zero and set `mbuf.l4_len` to zero, because the L4 header is only present in the first fragment.
3. WHEN a valid IPv4 packet is the first fragment (Fragment_Offset = 0 and MF_Flag is set), THE Parser SHALL parse the L4 header normally and extract source and destination ports.
4. WHEN a valid IPv4 packet is not fragmented (Fragment_Offset = 0 and MF_Flag is clear), THE Parser SHALL parse the L4 header normally and the fragmentation flag in Flags_Field SHALL remain clear.
5. THE PacketMetadata SHALL store the 13-bit Fragment_Offset value as a 16-bit unsigned integer field.
6. THE PacketMetadata SHALL use bit 1 of the Flags_Field to indicate that the packet is a fragment (either first or subsequent).

### Requirement 2: IPv4 Options Awareness

**User Story:** As a packet processor developer, I want the parser to correctly handle IPv4 packets with IP options, so that L4 header parsing starts at the correct offset regardless of options presence.

#### Acceptance Criteria

1. WHEN a valid IPv4 packet has IHL > 5, THE Parser SHALL compute the IP header length as IHL × 4 bytes and use that length to locate the L4 header.
2. WHEN a valid IPv4 packet has IHL > 5, THE Parser SHALL set a flag in the Flags_Field of PacketMetadata indicating that IPv4 options are present.
3. WHEN a valid IPv4 packet has IHL = 5, THE Parser SHALL leave the IPv4 options flag clear in the Flags_Field.
4. THE PacketMetadata SHALL use bit 2 of the Flags_Field to indicate that IPv4 options are present.
5. WHEN the computed IP header length (IHL × 4) extends beyond the available mbuf data, THE Parser SHALL return a kTooShort error.

### Requirement 3: IPv6 Extension Header Chain Walking

**User Story:** As a packet processor developer, I want the parser to walk the IPv6 extension header chain, so that the correct upper-layer protocol and L4 header offset are determined even when extension headers are present.

#### Acceptance Criteria

1. WHEN a valid IPv6 packet contains extension headers (Hop-by-Hop Options, Routing, Destination Options), THE Parser SHALL iterate through the chain using each header's Next Header and Header Extension Length fields until a non-extension-header protocol is reached.
2. WHEN the Parser encounters an IPv6_Fragment_Header (Next Header = 44), THE Parser SHALL extract the fragment offset and MF flag, set the fragmentation flag (bit 1) in the Flags_Field, and store the Fragment_Offset value.
3. WHEN an IPv6 packet is a non-first fragment (Fragment_Offset > 0), THE Parser SHALL set source and destination L4 ports to zero and set `mbuf.l4_len` to zero.
4. WHEN an IPv6 packet is the first fragment (Fragment_Offset = 0 and MF_Flag is set), THE Parser SHALL continue walking the extension header chain and parse the L4 header normally.
5. THE Parser SHALL set `mbuf.l3_len` to the total length of the fixed IPv6 header plus all extension headers traversed.
6. WHEN the extension header chain exceeds the available mbuf data length, THE Parser SHALL return a kTooShort error.
7. THE Parser SHALL recognize the following Next Header values as extension headers to walk: 0 (Hop-by-Hop), 43 (Routing), 44 (Fragment), 60 (Destination Options).
8. WHEN the Parser encounters Next Header values 51 (AH) or 50 (ESP), THE Parser SHALL stop walking and set the protocol field to that value, treating the packet as having an unsupported upper-layer protocol for L4 port extraction (ports set to zero).
9. THE PacketMetadata SHALL use bit 3 of the Flags_Field to indicate that one or more IPv6 extension headers were present.

### Requirement 4: QinQ (Double VLAN) Metadata Extraction

**User Story:** As a packet processor developer, I want the parser to extract VLAN tag information from single-tagged and QinQ double-tagged packets, so that VLAN-aware forwarding decisions can use the parsed VLAN IDs.

#### Acceptance Criteria

1. WHEN a packet has a single 802.1Q VLAN tag (EtherType 0x8100), THE Parser SHALL extract the 12-bit VLAN ID and store it in PacketMetadata.
2. WHEN a packet has QinQ double VLAN tags (outer EtherType 0x88A8 followed by inner EtherType 0x8100), THE Parser SHALL extract both the outer and inner 12-bit VLAN IDs and store them in PacketMetadata.
3. WHEN a packet has no VLAN tags, THE Parser SHALL set the VLAN tag count to zero and VLAN ID fields to zero.
4. THE PacketMetadata SHALL store a VLAN tag count (0, 1, or 2) as a 2-bit value, an outer VLAN ID as a 16-bit unsigned integer, and an inner VLAN ID as a 16-bit unsigned integer.
5. THE Parser SHALL continue to compute `mbuf.l2_len` correctly for packets with zero, one, or two VLAN tags (14, 18, or 22 bytes respectively).

### Requirement 5: PacketMetadata Struct Extension

**User Story:** As a packet processor developer, I want the PacketMetadata struct to have fields for the new parsed information, so that downstream consumers can access fragmentation status, options presence, extension header info, and VLAN IDs.

#### Acceptance Criteria

1. THE PacketMetadata SHALL add a `frag_offset` field (16-bit unsigned integer) storing the fragment offset value (in 8-byte units) for both IPv4 and IPv6 fragments, set to zero for non-fragmented packets.
2. THE PacketMetadata SHALL add an `outer_vlan_id` field (16-bit unsigned integer) and an `inner_vlan_id` field (16-bit unsigned integer).
3. THE PacketMetadata SHALL add a `vlan_count` field (8-bit unsigned integer) storing the number of VLAN tags (0, 1, or 2).
4. THE Flags_Field bit assignments SHALL be: bit 0 = IPv6, bit 1 = fragment, bit 2 = IPv4 options present, bit 3 = IPv6 extension headers present. Bits 4–63 remain reserved and SHALL be initialized to zero.
5. THE PacketMetadata struct size SHALL remain within the mbuf headroom constraint verified by the existing `static_assert` in `packet.h`.

### Requirement 6: HexPacket Test Utility Class

**User Story:** As a test developer, I want a utility class that converts a hex-encoded string into a DPDK mbuf, so that I can write packet parser tests using pre-generated packet data without manually constructing headers byte by byte.

#### Acceptance Criteria

1. THE HexPacket class SHALL accept a `const char*` hex string where each byte is represented as two hexadecimal characters (e.g., `"4500002800010000400600..."`) and convert it into raw bytes stored in a DPDK mbuf.
2. WHEN the hex string contains an odd number of characters, THE HexPacket class SHALL reject the input and signal an error.
3. WHEN the hex string contains characters outside the set [0-9a-fA-F], THE HexPacket class SHALL reject the input and signal an error.
4. THE HexPacket class SHALL accept a reference to a TestMbufAllocator and use it to allocate the mbuf.
5. THE HexPacket class SHALL set `mbuf.data_len` and `mbuf.pkt_len` to the number of decoded bytes.
6. THE HexPacket class SHALL provide a method that returns a `Packet&` reference suitable for passing to `PacketMetadata::Parse`.
7. THE HexPacket class SHALL optionally accept `packet_type` and `ol_flags` values to simulate hardware classification on the mbuf.

### Requirement 7: Scapy-Generated Test Data Convention

**User Story:** As a test developer, I want a clear convention for adding scapy-generated test packets, so that test data is reproducible and new test cases can be added by running a scapy command and pasting the output.

#### Acceptance Criteria

1. WHEN a hex string constant is defined in the test file, THE test file SHALL include a comment immediately above the constant showing the exact scapy Python code that generates the hex string (e.g., `// scapy: bytes(Ether()/IP(src="10.0.0.1")/TCP(sport=1234)).hex()`).
2. THE test file SHALL define hex string constants as `static constexpr const char*` variables with descriptive names indicating the packet type (e.g., `kIpv4TcpPacket`, `kIpv4FragSecondPacket`, `kQinQIpv6UdpPacket`).
3. THE test file SHALL include hex string constants covering at minimum: a basic IPv4/TCP packet, a basic IPv4/UDP packet, a basic IPv6/TCP packet, a basic IPv6/UDP packet, an IPv4 first fragment, an IPv4 non-first fragment, an IPv4 packet with options, an IPv6 packet with a Fragment header, an IPv6 packet with Hop-by-Hop and Routing extension headers, a single VLAN tagged packet, a QinQ double-tagged packet, a VXLAN encapsulated packet, an IPv4/ICMP Echo Request packet, an IPv4/ICMP Echo Reply packet, an IPv6/ICMPv6 Echo Request packet, and an ICMP Destination Unreachable (non-echo) packet.
4. FOR ALL hex string constants in the test file, converting the hex string to bytes and back to a hex string SHALL produce the identical string (round-trip property of the HexPacket decoder).
5. THE test file SHALL include malformed packet hex string constants covering at minimum: a packet truncated before the Ethernet header completes (fewer than 14 bytes), a packet truncated before the IPv4 header completes (valid Ethernet but fewer than 20 bytes of IP header), a packet truncated before the L4 header completes (valid Ethernet and IP headers but insufficient bytes for the TCP/UDP/ICMP header), an IPv4 packet where total_length exceeds the actual data length, an IPv6 packet where payload_len exceeds the actual data length, an IPv4 packet with IHL less than 5, a packet with an IP version field that is neither 4 nor 6, an IPv4/UDP packet where the UDP dgram_len field does not match the expected length from the IP total_length, a VXLAN-encapsulated packet truncated mid-VXLAN header (valid outer Ethernet/IP/UDP but fewer than 8 bytes of VXLAN header remaining), and a packet with ol_flags containing RTE_MBUF_F_RX_IP_CKSUM_BAD or RTE_MBUF_F_RX_L4_CKSUM_BAD set.
6. WHEN a malformed packet hex string constant is defined in the test file, THE test file SHALL include a comment immediately above the constant explaining the specific malformation and the expected ParseResult error code.

### Requirement 8: TestPacketBuilder Deprecation

**User Story:** As a test developer, I want to replace the manual TestPacketBuilder with the HexPacket approach, so that test packet construction is simpler, less error-prone, and directly traceable to scapy definitions.

#### Acceptance Criteria

1. THE new test file SHALL use HexPacket with static hex string constants for all deterministic test cases instead of TestPacketBuilder.
2. THE TestPacketBuilder class SHALL remain available for property-based tests (rapidcheck) that require runtime-generated packets with random parameters, because hex strings cannot represent dynamically generated packets.
3. WHEN a test requires a malformed packet (e.g., truncated, bad IHL, wrong version), THE test file SHALL define a hex string constant with a comment explaining the malformation and the scapy code or manual edit that produced it.
4. THE HexPacket class and associated hex string constants SHALL be defined in separate header files from TestPacketBuilder to allow independent inclusion.

### Requirement 9: ICMP and ICMPv6 Parsing

**User Story:** As a packet processor developer, I want the parser to extract meaningful fields from ICMP (IPv4) and ICMPv6 (IPv6) packets, so that downstream stages can identify and classify ICMP traffic using the same 5-tuple structure used for TCP and UDP.

#### Acceptance Criteria

1. WHEN a valid IPv4 packet has protocol number 1 (ICMP), THE Parser SHALL parse the ICMP_Header and extract the Type and Code fields.
2. WHEN a valid IPv6 packet has protocol number 58 (ICMPv6), THE Parser SHALL parse the ICMP_Header and extract the Type and Code fields.
3. WHEN an ICMP or ICMPv6 packet is an Echo Request or Echo Reply (IPv4 Type 8 or 0, IPv6 Type 128 or 129), THE Parser SHALL store the 16-bit Identifier field from the ICMP_Header in the `src_port` field of PacketMetadata.
4. WHEN an ICMP or ICMPv6 packet is not an Echo Request or Echo Reply, THE Parser SHALL set the `src_port` field of PacketMetadata to zero.
5. THE Parser SHALL store the ICMP Type and Code as a 16-bit value in the `dst_port` field of PacketMetadata, with the Type in the high byte and the Code in the low byte (i.e., `(type << 8) | code`).
6. THE Parser SHALL set `mbuf.l4_len` to 8 bytes (the minimum ICMP header size) for ICMP and ICMPv6 packets.
7. WHEN the available data after the IP header is fewer than 8 bytes for an ICMP or ICMPv6 packet, THE Parser SHALL return a kTooShort error.
8. THE Parser SHALL recognize protocol number 1 as ICMP for IPv4 and protocol number 58 as ICMPv6 for IPv6.
