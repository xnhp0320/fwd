# Implementation Plan: Packet Metadata

## Overview

Incrementally build the `PacketMetadata` struct, its `Parse` function, integrate it into the `Packet` class, add `PrefetchForEach` to `Batch`, and wire everything together with property-based and unit tests. Each task builds on the previous one so there is no orphaned code.

## Tasks

- [x] 1. Define core types and Packet integration
  - [x] 1.1 Create `rxtx/packet_metadata.h` with `IpAddress` union, `ParseResult` enum, and `PacketMetadata` struct
    - Define `IpAddress` union with `uint32_t v4` and `uint8_t v6[16]`
    - Define `ParseResult` enum class backed by `uint8_t` with values: `kOk`, `kChecksumError`, `kTooShort`, `kLengthMismatch`, `kMalformedHeader`, `kUnsupportedVersion`, `kUdpLengthMismatch`
    - Define `PacketMetadata` struct with fields: `src_ip`, `dst_ip`, `src_port`, `dst_port`, `vni`, `protocol`, `flags` (layout per design data model table)
    - Add `IsIpv6()` inline method returning `flags & 1u`
    - Declare `static ParseResult Parse(Packet& pkt, PacketMetadata& meta);`
    - Add `friend class Packet;`
    - Forward-declare `class Packet;` at top of header
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

  - [x] 1.2 Modify `rxtx/packet.h` to integrate `PacketMetadata`
    - Include `rxtx/packet_metadata.h`
    - Change `kMetadataSize` from `0` to `sizeof(PacketMetadata)`
    - Replace the commented-out metadata array with a real `PacketMetadata metadata_` member after `mbuf_`
    - Add `Metadata()` accessor returning `PacketMetadata&` (const and non-const overloads)
    - Add inline `Prefetch()` method calling `rte_prefetch0(Data())` and `rte_prefetch0(&metadata_)`
    - Add `friend struct PacketMetadata;` declaration
    - Verify existing `static_assert` still passes with new `kMetadataSize`
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 7.3, 7.5_

  - [x] 1.3 Update `rxtx/BUILD` with `packet_metadata` library target
    - Add `cc_library` for `packet_metadata` with `hdrs = ["packet_metadata.h"]` and `deps = ["//rxtx:packet"]`
    - Update `packet` library deps to include `packet_metadata` if needed for the circular include, or use forward declarations to break the cycle
    - _Requirements: 5.2_

- [x] 2. Implement Parse function
  - [x] 2.1 Create `rxtx/packet_metadata.cc` with `Parse` implementation
    - Implement checksum validation: check `mbuf.ol_flags` for `RTE_MBUF_F_RX_IP_CKSUM_BAD` and `RTE_MBUF_F_RX_L4_CKSUM_BAD`, return `kChecksumError`
    - Implement `packet_type` fast path: use `RTE_PTYPE_*` masks to detect L2/L3/L4/tunnel types when `packet_type != 0`
    - Implement manual header inspection fallback when `packet_type == 0`: parse Ethernet header, handle VLAN tags (0x8100/0x88A8), determine IP version from `ether_type`
    - Implement IPv4 parsing: extract IHL, validate IHL >= 5, extract total length, validate against `data_len`, extract source/dest IP, protocol
    - Implement IPv6 parsing: extract payload length, validate against `data_len`, extract source/dest IPv6 addresses, next header
    - Implement L4 parsing: for TCP/UDP extract ports; for other protocols set ports to zero
    - Implement UDP length validation: verify UDP length field == IP total length - IP header length
    - Implement VXLAN detection and parsing: check UDP dst port 4789, extract VNI, set outer layer lengths, parse inner Ethernet/IP/L4 headers
    - Populate mbuf layer-length fields: `l2_len`, `l3_len`, `l4_len`, `outer_l2_len`, `outer_l3_len`
    - Populate `PacketMetadata` fields: `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `flags` (bit 0 for IPv6, bits 1-63 zero), `vni`
    - Add `packet_metadata` cc_library `srcs` in BUILD or create separate build target
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 3.1, 3.2, 3.3, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 6.1, 6.2, 6.3_

- [x] 3. Create test packet builder
  - [x] 3.1 Create `rxtx/test_packet_builder.h` helper
    - Implement a builder class that constructs valid raw packet bytes in an mbuf
    - Support building Ethernet + IPv4 + TCP/UDP headers with configurable fields (src/dst IP, ports, protocol)
    - Support building Ethernet + IPv6 + TCP/UDP headers
    - Support building VXLAN-encapsulated packets (outer Ethernet + outer IPv4/UDP + VXLAN header + inner Ethernet + inner IP + inner L4)
    - Support VLAN tag insertion
    - Support setting `mbuf.packet_type` to simulate hardware classification
    - Support setting `mbuf.ol_flags` to simulate checksum offload results
    - Support injecting malformed fields: truncated length, bad IHL, bad IP version, UDP length mismatch, IP total length mismatch
    - Compute correct header lengths and total lengths automatically for valid packets
    - Add `test_packet_builder` library target in `rxtx/BUILD` (testonly)
    - _Requirements: 2.1, 2.2, 2.3, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

- [x] 4. Checkpoint - Verify core implementation compiles
  - Ensure all targets build cleanly, ask the user if questions arise.

- [x] 5. Property tests for Parse
  - [x] 5.1 Create `rxtx/packet_metadata_test.cc` with test fixture and rapidcheck setup
    - Set up GoogleTest fixture with DPDK EAL init via `rxtx::testing::InitEal()`
    - Include `rapidcheck.h` and `rapidcheck/gtest.h`
    - Add `packet_metadata_test` cc_test target in `rxtx/BUILD` with deps on `packet_metadata`, `test_utils`, `test_packet_builder`, `@googletest//:gtest`, `@rapidcheck`
    - _Requirements: 2.1_

  - [ ]* 5.2 Write property test: IPv4 parse round-trip
    - **Property 1: IPv4 parse round-trip**
    - Generate random valid non-tunneled IPv4/TCP and IPv4/UDP packets using test builder
    - Verify `src_ip.v4`, `dst_ip.v4`, `src_port`, `dst_port`, `protocol` match constructed values
    - Verify `flags` bit 0 == 0, `vni` == 0
    - Verify `mbuf.l2_len`, `mbuf.l3_len`, `mbuf.l4_len` match constructed header sizes
    - **Validates: Requirements 2.1, 1.4, 1.6, 3.1**

  - [ ]* 5.3 Write property test: IPv6 parse round-trip
    - **Property 2: IPv6 parse round-trip**
    - Generate random valid non-tunneled IPv6/TCP and IPv6/UDP packets
    - Verify `src_ip.v6`, `dst_ip.v6`, `src_port`, `dst_port`, `protocol` match constructed values
    - Verify `flags` bit 0 == 1, `vni` == 0
    - Verify `mbuf.l2_len`, `mbuf.l3_len`, `mbuf.l4_len` match constructed header sizes
    - **Validates: Requirements 2.2, 1.4, 3.1**

  - [ ]* 5.4 Write property test: VXLAN parse round-trip
    - **Property 3: VXLAN parse round-trip**
    - Generate random valid VXLAN packets with random outer headers, 24-bit VNI, inner IPv4 or IPv6 5-tuple
    - Verify inner 5-tuple fields match, `vni` matches, `flags` bit 0 reflects inner IP version
    - Verify `mbuf.outer_l2_len`, `mbuf.outer_l3_len` match outer headers, `mbuf.l2_len`, `mbuf.l3_len`, `mbuf.l4_len` match inner headers
    - **Validates: Requirements 2.3, 2.4, 2.5, 3.2, 3.3**

  - [ ]* 5.5 Write property test: Reserved flags bits are zero
    - **Property 4: Reserved flags bits are zero**
    - Generate random valid packets (IPv4, IPv6, tunneled, non-tunneled)
    - After successful parse, verify `(flags >> 1) == 0`
    - **Validates: Requirements 1.5**

  - [ ]* 5.6 Write property test: Non-TCP/UDP ports are zeroed
    - **Property 5: Non-TCP/UDP ports are zeroed**
    - Generate random valid packets with IP protocol != 6 and != 17 (e.g., ICMP, GRE)
    - After parsing, verify `src_port == 0` and `dst_port == 0`
    - **Validates: Requirements 2.6**

  - [ ]* 5.7 Write property test: Checksum error detection
    - **Property 6: Checksum error detection**
    - Generate random packets with `RTE_MBUF_F_RX_IP_CKSUM_BAD` or `RTE_MBUF_F_RX_L4_CKSUM_BAD` set in `ol_flags`
    - Verify `Parse` returns `kChecksumError`
    - **Validates: Requirements 4.1, 4.2**

  - [ ]* 5.8 Write property test: Too-short packet detection
    - **Property 7: Too-short packet detection**
    - Generate packets with `data_len` less than minimum required (34 bytes IPv4, 54 bytes IPv6)
    - Verify `Parse` returns `kTooShort`
    - **Validates: Requirements 4.3**

  - [ ]* 5.9 Write property test: IP total length exceeds data length
    - **Property 8: IP total length exceeds data length**
    - Generate packets where IP total length field > `data_len - l2_len`
    - Verify `Parse` returns `kLengthMismatch`
    - **Validates: Requirements 4.4**

  - [ ]* 5.10 Write property test: Malformed IPv4 IHL detection
    - **Property 9: Malformed IPv4 IHL detection**
    - Generate IPv4 packets with IHL < 5
    - Verify `Parse` returns `kMalformedHeader`
    - **Validates: Requirements 4.5**

  - [ ]* 5.11 Write property test: Unsupported IP version detection
    - **Property 10: Unsupported IP version detection**
    - Generate packets with IP version nibble in {0,1,2,3,5,7,...,15}
    - Verify `Parse` returns `kUnsupportedVersion`
    - **Validates: Requirements 4.6**

  - [ ]* 5.12 Write property test: UDP length mismatch detection
    - **Property 11: UDP length mismatch detection**
    - Generate UDP packets where UDP length field != IP total length - IP header length
    - Verify `Parse` returns `kUdpLengthMismatch`
    - **Validates: Requirements 4.7**

  - [ ]* 5.13 Write property test: packet_type fallback equivalence
    - **Property 12: packet_type fallback equivalence**
    - Generate random valid packets, parse once with original `packet_type`, parse again with `packet_type = 0`
    - Verify both produce identical `PacketMetadata` fields and identical mbuf layer-length values
    - **Validates: Requirements 6.1, 6.2, 6.3**

- [x] 6. Checkpoint - Verify Parse tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Add Packet layout and Prefetch tests
  - [x] 7.1 Add unit tests to `rxtx/packet_test.cc` for Metadata accessor and Prefetch
    - Test that `Metadata()` returns a reference at the expected offset after `rte_mbuf` (address of metadata == address of mbuf + kMbufStructSize)
    - Test that `Prefetch()` can be called without crashing (smoke test)
    - Test that `kMetadataSize == sizeof(PacketMetadata)` at runtime matches the static_assert
    - Update `packet_test` deps in `rxtx/BUILD` to include `packet_metadata`
    - _Requirements: 5.3, 5.4, 5.5, 7.3_

- [x] 8. Implement PrefetchForEach on Batch
  - [x] 8.1 Add `PrefetchForEach<N>` template method to `rxtx/batch.h`
    - Implement the method templated on `uint16_t N` and callable `Fn`
    - Iterate [0, Count()), prefetch packet at `i+N` when `i+N < Count()` using `Packet::Prefetch()`
    - Use `if constexpr (N > 0)` to eliminate prefetch branch when N == 0
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_

  - [ ]* 8.2 Write property test: PrefetchForEach visits all packets in order
    - **Property 13: PrefetchForEach visits all packets in order**
    - Generate random batch sizes in [0, BatchSize), random prefetch distance N in [0, BatchSize)
    - Verify `PrefetchForEach<N>` invokes callback on every packet in index order [0, Count()), producing same sequence as `ForEach`
    - Add to `rxtx/batch_test.cc`
    - **Validates: Requirements 7.1, 7.6**

  - [ ]* 8.3 Write unit tests for PrefetchForEach in `rxtx/batch_test.cc`
    - Test `PrefetchForEach<0>` on empty batch: no callback invocations
    - Test `PrefetchForEach<4>` on a batch with known packets: verify all visited in order
    - Update `batch_test` deps in `rxtx/BUILD` to include `packet_metadata` and `@rapidcheck`
    - _Requirements: 7.1, 7.6_

- [x] 9. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Property tests use rapidcheck with GoogleTest integration (`rapidcheck/gtest.h`)
- The test packet builder (task 3.1) is a prerequisite for all property tests
- `static_assert` in `packet.h` provides compile-time verification of metadata size constraint (Requirement 5.5)
- Checkpoints at tasks 4, 6, and 9 ensure incremental validation
