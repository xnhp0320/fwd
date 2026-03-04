# Implementation Plan: Parser and Test Builder Enhancement

## Overview

Extend the `PacketMetadata` parser with IPv4 fragmentation, IPv4 options, IPv6 extension header walking, QinQ VLAN extraction, and ICMP/ICMPv6 parsing. Introduce a `HexPacket` test utility and scapy-generated test data constants for all deterministic unit tests. Retain `TestPacketBuilder` solely for rapidcheck property-based tests (which need runtime-generated packets with random parameters), extending it with new builder methods for the enhanced parser features.

## Tasks

- [x] 1. Extend PacketMetadata struct and add MetaFlag enum
  - [x] 1.1 Add new fields and MetaFlag enum to `rxtx/packet_metadata.h`
    - Add `vlan_count` (uint8_t), `frag_offset` (uint16_t), `outer_vlan_id` (uint16_t), `inner_vlan_id` (uint16_t) fields after `protocol`, replacing the 7-byte padding gap
    - Define `enum MetaFlag : uint64_t` with `kFlagIpv6 = 1u << 0`, `kFlagFragment = 1u << 1`, `kFlagIpv4Options = 1u << 2`, `kFlagIpv6ExtHeaders = 1u << 3`
    - Add helper methods: `IsFragment()`, `HasIpv4Options()`, `HasIpv6ExtHeaders()`
    - Update the existing `IsIpv6()` to use `kFlagIpv6` instead of the magic `1u`
    - Verify struct remains 56 bytes with a `static_assert(sizeof(PacketMetadata) == 56)`
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

  - [x] 1.2 Zero-initialize new fields in `PacketMetadata::Parse()` in `rxtx/packet_metadata.cc`
    - Set `meta.frag_offset = 0`, `meta.outer_vlan_id = 0`, `meta.inner_vlan_id = 0`, `meta.vlan_count = 0` before calling `ParseLayer()`
    - Replace magic protocol number constants (`kProtoTcp = 6`, `kProtoUdp = 17`) with POSIX `IPPROTO_TCP`, `IPPROTO_UDP` from `<netinet/in.h>`
    - Update all `flags` assignments to use `MetaFlag` enum values (e.g., `kFlagIpv6` instead of `1u`)
    - _Requirements: 5.4_

- [x] 2. Implement VLAN metadata extraction in ParseLayer
  - [x] 2.1 Extract VLAN IDs during the existing VLAN tag loop in `ParseLayer()` in `rxtx/packet_metadata.cc`
    - On first VLAN tag (0x8100 or 0x88A8): extract 12-bit VLAN ID from `vlan_tci`, store in `meta.outer_vlan_id`, set `meta.vlan_count = 1`
    - On second VLAN tag: store in `meta.inner_vlan_id`, set `meta.vlan_count = 2`
    - Ensure `mbuf.l2_len` is correctly computed as `14 + (4 × vlan_count)` (already handled by the existing `l2_len` accumulation)
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

- [x] 3. Implement IPv4 fragmentation detection and options awareness
  - [x] 3.1 Add IPv4 fragmentation detection in the IPv4 branch of `ParseLayer()` in `rxtx/packet_metadata.cc`
    - After reading the IPv4 header, extract the 16-bit flags+fragment_offset field
    - Compute `frag_off` (13-bit offset) and `mf` (More Fragments bit)
    - If `frag_off != 0 || mf`: set `meta.flags |= kFlagFragment` and `meta.frag_offset = frag_off`
    - If `frag_off != 0` (non-first fragment): set `src_port = 0`, `dst_port = 0`, `out_l4_len = 0`, return `kOk` (skip L4 parsing)
    - First fragments (`frag_off == 0 && mf`) continue to L4 parsing normally
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

  - [x] 3.2 Add IPv4 options flag in the IPv4 branch of `ParseLayer()`
    - After computing `ip_hdr_len = ihl * 4`, if `ihl > 5`: set `meta.flags |= kFlagIpv4Options`
    - The existing L4 offset calculation already uses `ihl * 4`, so options are correctly skipped
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 4. Implement IPv6 extension header chain walking
  - [x] 4.1 Add `IsIpv6ExtensionHeader()` helper and extension header walking loop in the IPv6 branch of `ParseLayer()` in `rxtx/packet_metadata.cc`
    - Add static helper `IsIpv6ExtensionHeader(uint8_t next_hdr)` recognizing `IPPROTO_HOPOPTS` (0), `IPPROTO_ROUTING` (43), `IPPROTO_FRAGMENT` (44), `IPPROTO_ESP` (50), `IPPROTO_AH` (51), `IPPROTO_DSTOPTS` (60)
    - Replace the fixed `out_l3_len = kIpv6HdrLen` with a loop that walks the extension header chain
    - For Fragment header (44): extract frag_offset and MF flag, set `kFlagFragment`, store `frag_offset`; fragment header is always 8 bytes
    - For AH (51) or ESP (50): stop walking, set protocol, set ports to zero
    - For Hop-by-Hop (0), Routing (43), Destination Options (60): read Next Header and Header Extension Length, compute size as `(ext_len + 1) * 8`
    - Set `kFlagIpv6ExtHeaders` if any extension headers were traversed
    - Set `out_l3_len = kIpv6HdrLen + total_ext_len`
    - Return `kTooShort` if extension header chain exceeds available data
    - For non-first IPv6 fragments (`frag_off > 0`): set ports to zero, `l4_len = 0`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

- [x] 5. Implement ICMP and ICMPv6 parsing
  - [x] 5.1 Add ICMP/ICMPv6 L4 parsing branch in `ParseLayer()` in `rxtx/packet_metadata.cc`
    - Define ICMP type constants: `kIcmpEchoReply = 0`, `kIcmpEchoRequest = 8`, `kIcmpv6EchoRequest = 128`, `kIcmpv6EchoReply = 129`
    - Add `else if (meta.protocol == IPPROTO_ICMP || meta.protocol == IPPROTO_ICMPV6)` branch in both IPv4 and IPv6 L4 sections
    - Check `l4_offset + 8 > data_len` → return `kTooShort`
    - Extract type and code: `meta.dst_port = (type << 8) | code`
    - For Echo Request/Reply: `meta.src_port = identifier` (bytes 4-5 of ICMP header)
    - For non-echo: `meta.src_port = 0`
    - Set `out_l4_len = 8`
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 9.7, 9.8_

- [x] 6. Checkpoint — verify parser compiles and existing tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Create HexPacket test utility class
  - [x] 7.1 Create `rxtx/hex_packet.h` with the `HexPacket` class
    - Header-only class in `rxtx::testing` namespace
    - Constructor takes `const char* hex`, `TestMbufAllocator& alloc`, optional `uint32_t packet_type = 0`, optional `uint64_t ol_flags = 0`
    - Validate hex string: reject odd length, reject non-hex characters; set `valid_ = false` on error
    - Decode hex pairs to bytes, allocate mbuf via `alloc.Alloc()`, copy bytes with `rte_pktmbuf_append()` + `memcpy`
    - Set `mbuf->packet_type` and `mbuf->ol_flags` if provided
    - Provide `GetPacket()` returning `Packet&`, `Length()` returning decoded byte count, `Valid()` returning construction status
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7_

  - [x] 7.2 Add `hex_packet` library target to `rxtx/BUILD`
    - Add `cc_library` for `hex_packet` with `hdrs = ["hex_packet.h"]`, deps on `//rxtx:packet`, `//rxtx:test_utils`, `//:dpdk_lib`, `testonly = True`
    - _Requirements: 8.4_

- [x] 8. Create scapy-generated test data constants
  - [x] 8.1 Create `rxtx/test_packet_data.h` with all hex string constants
    - Define `static constexpr const char*` constants, each preceded by a scapy comment
    - Valid packets: `kIpv4TcpPacket`, `kIpv4UdpPacket`, `kIpv6TcpPacket`, `kIpv6UdpPacket`, `kIpv4FragFirstPacket`, `kIpv4FragSecondPacket`, `kIpv4OptionsPacket`, `kIpv6FragmentPacket`, `kIpv6ExtHdrPacket`, `kVlanIpv4TcpPacket`, `kQinQIpv6UdpPacket`, `kVxlanIpv4TcpPacket`, `kIcmpEchoRequestPacket`, `kIcmpEchoReplyPacket`, `kIcmpv6EchoRequestPacket`, `kIcmpDestUnreachPacket`
    - Malformed packets: `kTruncatedEthernetPacket`, `kTruncatedIpv4Packet`, `kTruncatedL4Packet`, `kIpv4TotalLenExceedsPacket`, `kIpv6PayloadLenExceedsPacket`, `kIpv4BadIhlPacket`, `kBadIpVersionPacket`, `kUdpLenMismatchPacket`, `kVxlanTruncatedPacket`, `kBadChecksumPacket` (with `ol_flags` note)
    - Each malformed constant has a comment explaining the malformation and expected `ParseResult`
    - _Requirements: 7.1, 7.2, 7.3, 7.5, 7.6_

  - [x] 8.2 Add `test_packet_data` library target to `rxtx/BUILD`
    - Add `cc_library` for `test_packet_data` with `hdrs = ["test_packet_data.h"]`, `testonly = True`
    - _Requirements: 8.4_

- [x] 9. Write HexPacket-based unit tests
  - [x] 9.1 Write deterministic unit tests in `rxtx/packet_metadata_test.cc` using HexPacket
    - Add `#include "rxtx/hex_packet.h"` and `#include "rxtx/test_packet_data.h"`
    - Write `TEST_F` cases for each valid packet constant: verify `ParseResult::kOk`, correct `src_ip`, `dst_ip`, `src_port`, `dst_port`, `protocol`, `vni`, `flags`, `vlan_count`, `outer_vlan_id`, `inner_vlan_id`, `frag_offset`, and `mbuf.l2_len`, `mbuf.l3_len`, `mbuf.l4_len`
    - Write `TEST_F` cases for each malformed packet constant: verify the expected `ParseResult` error code
    - Test ICMP field encoding: verify `dst_port = (type << 8) | code`, `src_port = identifier` for echo, `src_port = 0` for non-echo
    - Test fragmentation: verify fragment flag set, `frag_offset` value, port zeroing for non-first fragments, normal L4 parsing for first fragments
    - Test VLAN: verify `vlan_count`, `outer_vlan_id`, `inner_vlan_id`, `l2_len` for 0, 1, and 2 VLAN tags
    - Test IPv4 options: verify `kFlagIpv4Options` set when IHL > 5, correct L4 port extraction
    - Test IPv6 extension headers: verify `kFlagIpv6ExtHeaders` set, correct `l3_len`, correct protocol after chain walking
    - _Requirements: 7.3, 7.4, 7.5, 8.1, 8.3_

  - [x] 9.2 Update `packet_metadata_test` target in `rxtx/BUILD`
    - Add deps on `//rxtx:hex_packet` and `//rxtx:test_packet_data`
    - _Requirements: 8.4_

- [x] 10. Checkpoint — ensure all unit tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 11. Extend TestPacketBuilder for rapidcheck property tests
  - [x]* 11.1 Add fragmentation, options, IPv6 extension header, ICMP, and QinQ builder methods to `rxtx/test_packet_builder.h`
    - These extensions are needed only for rapidcheck property tests (task 12) which generate packets with random parameters at runtime. HexPacket cannot be used for property tests because hex strings are static constants — rapidcheck needs a builder that constructs arbitrary packets on the fly with whatever random values it generates.
    - Add `SetFragOffset(uint16_t)` and `SetMfFlag(bool)` for IPv4 fragmentation field injection
    - Add `SetIhl(uint8_t)` to write IPv4 options padding bytes (IHL > 5)
    - Add IPv6 extension header building: methods to prepend Hop-by-Hop, Routing, Fragment, Destination Options headers before the L4 header
    - Add ICMP header building: `BuildIpv4Icmp()` and `BuildIpv6Icmp()` methods that write type/code/identifier fields
    - Add QinQ support: `SetOuterVlanId(uint16_t)` and `SetInnerVlanId(uint16_t)` for double VLAN tagging
    - Update `L4HdrLen()` to return 8 for `IPPROTO_ICMP` and `IPPROTO_ICMPV6`
    - _Requirements: 8.2_

- [x] 12. Write property-based tests with rapidcheck
  - [x]* 12.1 Write property test for fragmentation flag correctness
    - **Property 1: Fragmentation flag correctness**
    - Use `TestPacketBuilder` with random `frag_offset` and `mf_flag` values for IPv4, and random IPv6 Fragment headers
    - Verify: fragment flag set iff packet is fragmented, `frag_offset` stores correct value, clear for non-fragmented
    - **Validates: Requirements 1.1, 1.4, 1.6, 3.2**

  - [x]* 12.2 Write property test for non-first fragment port zeroing
    - **Property 2: Non-first fragment port zeroing**
    - Use `TestPacketBuilder` with random `frag_offset > 0` for IPv4 and IPv6
    - Verify: `src_port == 0`, `dst_port == 0`, `l4_len == 0` for non-first fragments; normal ports for first fragments and unfragmented packets
    - **Validates: Requirements 1.2, 1.3, 3.3, 3.4**

  - [x]* 12.3 Write property test for IPv4 options flag and L4 offset correctness
    - **Property 3: IPv4 options flag and L4 offset correctness**
    - Use `TestPacketBuilder` with random IHL values (5–15) and TCP/UDP L4 headers
    - Verify: `kFlagIpv4Options` set iff IHL > 5, extracted ports match builder values regardless of options
    - **Validates: Requirements 2.1, 2.2, 2.3**

  - [x]* 12.4 Write property test for IPv6 extension header chain walking
    - **Property 4: IPv6 extension header chain walking**
    - Use `TestPacketBuilder` to generate IPv6 packets with random combinations of Hop-by-Hop, Routing, Fragment, Destination Options, terminated by TCP/UDP/ICMP or AH/ESP
    - Verify: correct protocol, `l3_len == 40 + total_ext_len`, `kFlagIpv6ExtHeaders` set, AH/ESP → ports zero
    - **Validates: Requirements 3.1, 3.5, 3.7, 3.8, 3.9**

  - [x]* 12.5 Write property test for VLAN metadata extraction
    - **Property 5: VLAN metadata extraction**
    - Use `TestPacketBuilder` with 0, 1, or 2 random VLAN tags
    - Verify: `vlan_count` matches tag count, `outer_vlan_id` and `inner_vlan_id` correct, `l2_len == 14 + 4 * vlan_count`
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.5**

  - [x]* 12.6 Write property test for HexPacket decode/encode round-trip
    - **Property 6: HexPacket decode/encode round-trip**
    - Generate random even-length hex strings, decode via `HexPacket`, re-encode mbuf data to hex, verify identical string
    - **Validates: Requirements 6.1, 7.4**

  - [x]* 12.7 Write property test for ICMP/ICMPv6 field extraction
    - **Property 7: ICMP/ICMPv6 field extraction**
    - Use `TestPacketBuilder` with random ICMP type/code/identifier for IPv4 ICMP and IPv6 ICMPv6
    - Verify: `dst_port == (type << 8) | code`, `src_port == identifier` for echo or `0` for non-echo, `l4_len == 8`
    - **Validates: Requirements 9.1, 9.2, 9.3, 9.4, 9.5, 9.6**

- [x] 13. Final checkpoint — ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases using scapy-generated hex data
- `TestPacketBuilder` is retained exclusively for rapidcheck property tests (task 12). Rapidcheck generates packets with random parameters at runtime — you can't express that with static hex strings. All deterministic tests use `HexPacket` instead.
- The design uses C++ throughout; all code examples use C++
