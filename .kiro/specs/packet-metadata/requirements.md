# Requirements Document

## Introduction

This feature introduces a `PacketMetadata` class in the `rxtx` module that stores parsed 5-tuple and VXLAN VNI information extracted from raw packets. The class provides a parser that fills DPDK mbuf layer-length fields (`l2_len`, `l3_len`, `l4_len`, and outer variants for VXLAN), detects malformed packets, and supports both IPv4 and IPv6 through a union-based IP address representation with a 64-bit flags field.

The `PacketMetadata` class is the first consumer of the reserved metadata region in the `Packet` class (`kMetadataSize`). It is a friend of `Packet` to allow direct mbuf manipulation during parsing.

## Glossary

- **PacketMetadata**: A class in the `rxtx` namespace that stores parsed 5-tuple and VXLAN VNI data extracted from a packet's mbuf.
- **Parser**: The `PacketMetadata::Parse` static or member function that inspects raw packet data and populates a `PacketMetadata` instance plus mbuf layer-length fields.
- **Packet**: The existing `rxtx::Packet` class that wraps an `rte_mbuf` via placement new.
- **Five_Tuple**: The combination of source IP address, destination IP address, source L4 port, destination L4 port, and IP protocol number.
- **VNI**: VXLAN Network Identifier, a 24-bit value identifying a VXLAN segment. Zero for non-VXLAN packets.
- **Mbuf**: DPDK `rte_mbuf` structure representing a network packet buffer.
- **Mbuf_Flags**: The `ol_flags` field of `rte_mbuf`, which includes hardware offload status bits such as `RTE_MBUF_F_RX_IP_CKSUM_BAD` and `RTE_MBUF_F_RX_L4_CKSUM_BAD`.
- **Packet_Type**: The `packet_type` field of `rte_mbuf`, populated by hardware or software classification to indicate L2/L3/L4 protocol types.
- **Layer_Lengths**: The `l2_len`, `l3_len`, `l4_len` fields of `rte_mbuf` that record the byte length of each protocol layer header.
- **Outer_Layer_Lengths**: The `outer_l2_len`, `outer_l3_len` fields of `rte_mbuf` used for tunneled (VXLAN) packets to record the outer encapsulation header lengths.
- **IP_Address_Union**: A union type that stores either an IPv4 address (32-bit) or an IPv6 address (128-bit) without a discriminator inside the union itself.
- **Flags_Field**: A 64-bit unsigned integer in `PacketMetadata` where bit 0 indicates IPv6 (set) vs IPv4 (clear) for the Five_Tuple, and remaining bits are reserved.
- **Batch**: The existing `rxtx::Batch` template class that holds an array of `rte_mbuf*` pointers and provides iteration methods (`ForEach`, `Filter`).
- **Prefetch_Distance**: A compile-time template parameter N that specifies how many packets ahead to prefetch during `PrefetchForEach` iteration.

## Requirements

### Requirement 1: Five-Tuple Storage

**User Story:** As a packet processor developer, I want a metadata class that stores parsed 5-tuple information, so that downstream processing stages can make forwarding and classification decisions based on flow identity.

#### Acceptance Criteria

1. THE PacketMetadata SHALL store source and destination IP addresses using an IP_Address_Union that holds either a 32-bit IPv4 address or a 128-bit IPv6 address.
2. THE PacketMetadata SHALL store source and destination L4 port numbers as 16-bit unsigned integers.
3. THE PacketMetadata SHALL store the IP protocol number as an 8-bit unsigned integer.
4. THE PacketMetadata SHALL store a 64-bit Flags_Field where bit 0 indicates whether the Five_Tuple contains IPv6 addresses (bit 0 set) or IPv4 addresses (bit 0 clear).
5. THE PacketMetadata SHALL reserve bits 1 through 63 of the Flags_Field for future use, and the Parser SHALL initialize those bits to zero.
6. THE PacketMetadata SHALL store the VNI as a 32-bit unsigned integer, set to zero for non-VXLAN packets.

### Requirement 2: Packet Parsing

**User Story:** As a packet processor developer, I want a parse function that extracts 5-tuple and VNI from a raw packet, so that I do not need to manually decode protocol headers in every processor.

#### Acceptance Criteria

1. WHEN a valid non-tunneled IPv4 packet is provided, THE Parser SHALL extract the source IPv4 address, destination IPv4 address, source L4 port, destination L4 port, IP protocol, set the Flags_Field bit 0 to zero, and set VNI to zero.
2. WHEN a valid non-tunneled IPv6 packet is provided, THE Parser SHALL extract the source IPv6 address, destination IPv6 address, source L4 port, destination L4 port, IP protocol, and set the Flags_Field bit 0 to one.
3. WHEN a valid VXLAN-encapsulated packet is provided, THE Parser SHALL extract the inner Five_Tuple and set VNI to the 24-bit VXLAN Network Identifier from the VXLAN header.
4. WHEN the inner payload of a VXLAN packet is IPv4, THE Parser SHALL set the Flags_Field bit 0 to zero.
5. WHEN the inner payload of a VXLAN packet is IPv6, THE Parser SHALL set the Flags_Field bit 0 to one.
6. WHEN the L4 protocol is neither TCP nor UDP, THE Parser SHALL set source and destination L4 ports to zero.

### Requirement 3: Mbuf Layer-Length Population

**User Story:** As a packet processor developer, I want the parser to fill mbuf layer-length fields, so that DPDK TX offload and other subsystems can use correct header boundaries.

#### Acceptance Criteria

1. WHEN a valid non-tunneled packet is parsed, THE Parser SHALL set `mbuf.l2_len` to the Ethernet header length (including any VLAN tags), `mbuf.l3_len` to the IP header length, and `mbuf.l4_len` to the L4 header length.
2. WHEN a valid VXLAN-encapsulated packet is parsed, THE Parser SHALL set `mbuf.outer_l2_len` to the outer Ethernet header length, `mbuf.outer_l3_len` to the outer IP header length, `mbuf.l2_len` to the inner Ethernet header length, `mbuf.l3_len` to the inner IP header length, and `mbuf.l4_len` to the inner L4 header length.
3. WHEN a VXLAN-encapsulated packet is parsed, THE Parser SHALL set `mbuf.l2_len` and related inner fields based on the inner packet headers, not the outer headers.

### Requirement 4: Malformed Packet Detection

**User Story:** As a packet processor developer, I want the parser to detect and reject malformed packets, so that downstream stages only process well-formed traffic.

#### Acceptance Criteria

1. WHEN the Mbuf_Flags indicate an IP checksum error (`RTE_MBUF_F_RX_IP_CKSUM_BAD`), THE Parser SHALL return an error status indicating a checksum failure.
2. WHEN the Mbuf_Flags indicate a TCP or UDP checksum error (`RTE_MBUF_F_RX_L4_CKSUM_BAD`), THE Parser SHALL return an error status indicating a checksum failure.
3. WHEN the packet's mbuf data length is less than the minimum required for the detected Ethernet plus IP header, THE Parser SHALL return an error status indicating insufficient packet length.
4. WHEN the IP total length field exceeds the mbuf data length, THE Parser SHALL return an error status indicating a length mismatch.
5. WHEN the IP header length field (IPv4 IHL) specifies a value smaller than the minimum IP header size (20 bytes for IPv4), THE Parser SHALL return an error status indicating a malformed header.
6. WHEN the IP version field is neither 4 nor 6, THE Parser SHALL return an error status indicating an unsupported IP version.
7. WHEN the L4 protocol is UDP and the UDP header length field value does not match the expected UDP payload size derived from the IP total length minus the IP header length, THE Parser SHALL return an error status indicating a UDP length mismatch.

### Requirement 5: Packet Class Integration

**User Story:** As a packet processor developer, I want PacketMetadata to integrate with the existing Packet class, so that metadata is co-located with the mbuf for cache-friendly access.

#### Acceptance Criteria

1. THE PacketMetadata class SHALL be declared as a friend of the Packet class to allow the Parser to write mbuf layer-length fields directly.
2. THE Packet class SHALL increase `kMetadataSize` to accommodate the PacketMetadata structure.
3. THE PacketMetadata SHALL be stored in the metadata region of the Packet class, immediately following the `rte_mbuf` structure.
4. THE Packet class SHALL provide an accessor method that returns a reference to the PacketMetadata stored in the metadata region.
5. THE PacketMetadata structure size plus `kMbufStructSize` SHALL fit within the mbuf headroom, verified by a `static_assert`.

### Requirement 6: Packet Type Utilization

**User Story:** As a packet processor developer, I want the parser to leverage the mbuf `packet_type` field for efficient protocol detection, so that parsing avoids redundant header inspection when hardware classification is available.

#### Acceptance Criteria

1. WHEN the Packet_Type field contains valid L2, L3, and L4 type information, THE Parser SHALL use Packet_Type to determine protocol layers instead of re-parsing raw header bytes.
2. WHEN the Packet_Type field indicates a tunneled packet type (VXLAN), THE Parser SHALL use Packet_Type to identify the tunnel encapsulation.
3. IF the Packet_Type field is zero or does not contain sufficient classification, THEN THE Parser SHALL fall back to manual header inspection to determine protocol layers.

### Requirement 7: Prefetched Batch Iteration

**User Story:** As a packet processor developer, I want a prefetched batch iteration method on the Batch class, so that cache misses are hidden by prefetching upcoming packets during iteration.

#### Acceptance Criteria

1. THE Batch class SHALL provide a `PrefetchForEach` method templated on a compile-time prefetch distance N and a callable Fn, that iterates over packets in order [0, Count()).
2. WHEN processing the packet at index i, THE `PrefetchForEach` method SHALL prefetch the packet at index i+N if i+N is less than Count().
3. THE Packet class SHALL provide a `Prefetch` member function that prefetches the first 64 bytes of the region returned by `Packet::Data()` and the PacketMetadata region associated with the Packet.
4. THE `PrefetchForEach` method SHALL call `Packet::Prefetch` on the packet at index i+N to perform the prefetch.
5. THE `Prefetch` member function SHALL be defined inline in the header as a named function, keeping prefetch targets in one place while allowing the compiler to inline it into hot loops.
6. WHEN the prefetch distance N is zero, THE `PrefetchForEach` method SHALL behave identically to `ForEach` with no prefetch operations.
