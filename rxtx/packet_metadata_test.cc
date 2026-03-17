// rxtx/packet_metadata_test.cc
// Deterministic unit tests and property-based test setup for PacketMetadata::Parse.
// Unit tests use HexPacket with scapy-generated hex constants from test_packet_data.h.
// Property tests (tasks 12.x) will be added incrementally.

#include "rxtx/packet_metadata.h"
#include "rxtx/hex_packet.h"
#include "rxtx/packet.h"
#include "rxtx/test_packet_builder.h"
#include "rxtx/test_packet_data.h"
#include "rxtx/test_utils.h"

#include <cstring>
#include <netinet/in.h>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <rte_byteorder.h>
#include <rte_mbuf.h>

class PacketMetadataTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_TRUE(rxtx::testing::InitEal()) << "Failed to initialize DPDK EAL";
  }

  rxtx::testing::TestMbufAllocator alloc_;
};

// ============================================================================
// Valid packet tests
// ============================================================================

TEST_F(PacketMetadataTest, Ipv4Tcp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4TcpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(rte_be_to_cpu_32(meta.src_ip.v4), 0x0a000001u);  // 10.0.0.1
  EXPECT_EQ(rte_be_to_cpu_32(meta.dst_ip.v4), 0x0a000002u);  // 10.0.0.2
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  EXPECT_EQ(meta.vni, 0u);
  EXPECT_EQ(meta.flags, 0u);
  EXPECT_EQ(meta.vlan_count, 0);
  EXPECT_EQ(meta.frag_offset, 0);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(14));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(20));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, Ipv4Udp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4UdpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 53);
  EXPECT_EQ(meta.protocol, IPPROTO_UDP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(14));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(20));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

TEST_F(PacketMetadataTest, Ipv6Tcp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv6TcpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.IsIpv6());
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  // Check IPv6 src=::1
  uint8_t expected_src[16] = {};
  expected_src[15] = 1;
  EXPECT_EQ(std::memcmp(meta.src_ip.v6, expected_src, 16), 0);
  // Check IPv6 dst=::2
  uint8_t expected_dst[16] = {};
  expected_dst[15] = 2;
  EXPECT_EQ(std::memcmp(meta.dst_ip.v6, expected_dst, 16), 0);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(14));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(40));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, Ipv6Udp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv6UdpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.IsIpv6());
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 53);
  EXPECT_EQ(meta.protocol, IPPROTO_UDP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(14));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(40));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

TEST_F(PacketMetadataTest, Ipv4FragFirst) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4FragFirstPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.IsFragment());
  EXPECT_EQ(meta.frag_offset, 0);
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, Ipv4FragSecond) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4FragSecondPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.IsFragment());
  EXPECT_EQ(meta.frag_offset, 185);
  EXPECT_EQ(meta.src_port, 0);
  EXPECT_EQ(meta.dst_port, 0);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(0));
}

TEST_F(PacketMetadataTest, Ipv4Options) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4OptionsPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.HasIpv4Options());
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(24));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, Ipv6Fragment) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv6FragmentPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.IsFragment());
  EXPECT_TRUE(meta.HasIpv6ExtHeaders());
  EXPECT_EQ(meta.frag_offset, 0);
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(48));  // 40 + 8 fragment header
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, Ipv6ExtHdr) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv6ExtHdrPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(meta.HasIpv6ExtHeaders());
  EXPECT_FALSE(meta.IsFragment());
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(56));  // 40 + 8 HBH + 8 Routing
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, VlanIpv4Tcp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kVlanIpv4TcpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.vlan_count, 1);
  EXPECT_EQ(meta.outer_vlan_id, 100);
  EXPECT_EQ(meta.inner_vlan_id, 0);
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(18));
}

TEST_F(PacketMetadataTest, QinQIpv6Udp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kQinQIpv6UdpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.vlan_count, 2);
  EXPECT_EQ(meta.outer_vlan_id, 200);
  EXPECT_EQ(meta.inner_vlan_id, 300);
  EXPECT_TRUE(meta.IsIpv6());
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 53);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(22));
}

TEST_F(PacketMetadataTest, VxlanIpv4Tcp) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kVxlanIpv4TcpPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.vni, 42u);
  EXPECT_EQ(meta.src_port, 1234);
  EXPECT_EQ(meta.dst_port, 80);
  EXPECT_EQ(meta.protocol, IPPROTO_TCP);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l2_len, static_cast<uint64_t>(14));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l3_len, static_cast<uint64_t>(20));
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(20));
}

TEST_F(PacketMetadataTest, IcmpEchoRequest) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIcmpEchoRequestPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.protocol, IPPROTO_ICMP);
  EXPECT_EQ(meta.dst_port, (8 << 8) | 0);   // type=8, code=0 → 2048
  EXPECT_EQ(meta.src_port, 0x1234);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

TEST_F(PacketMetadataTest, IcmpEchoReply) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIcmpEchoReplyPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.protocol, IPPROTO_ICMP);
  EXPECT_EQ(meta.dst_port, (0 << 8) | 0);   // type=0, code=0 → 0
  EXPECT_EQ(meta.src_port, 0x5678);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

TEST_F(PacketMetadataTest, Icmpv6EchoRequest) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIcmpv6EchoRequestPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.protocol, IPPROTO_ICMPV6);
  EXPECT_TRUE(meta.IsIpv6());
  EXPECT_EQ(meta.dst_port, (128 << 8) | 0);  // type=128, code=0 → 32768
  EXPECT_EQ(meta.src_port, 0xabcd);
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

TEST_F(PacketMetadataTest, IcmpDestUnreach) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIcmpDestUnreachPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(meta.protocol, IPPROTO_ICMP);
  EXPECT_EQ(meta.dst_port, (3 << 8) | 1);   // type=3, code=1 → 769
  EXPECT_EQ(meta.src_port, 0);               // non-echo
  EXPECT_EQ(pkt.GetPacket().Mbuf()->l4_len, static_cast<uint64_t>(8));
}

// ============================================================================
// Malformed packet tests
// ============================================================================

TEST_F(PacketMetadataTest, TruncatedEthernet) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kTruncatedEthernetPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kTooShort);
}

TEST_F(PacketMetadataTest, TruncatedIpv4) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kTruncatedIpv4Packet, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kTooShort);
}

TEST_F(PacketMetadataTest, TruncatedL4) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kTruncatedL4Packet, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kTooShort);
}

TEST_F(PacketMetadataTest, Ipv4TotalLenExceeds) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4TotalLenExceedsPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kLengthMismatch);
}

TEST_F(PacketMetadataTest, Ipv6PayloadLenExceeds) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv6PayloadLenExceedsPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kLengthMismatch);
}

TEST_F(PacketMetadataTest, Ipv4BadIhl) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kIpv4BadIhlPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kMalformedHeader);
}

TEST_F(PacketMetadataTest, BadIpVersion) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kBadIpVersionPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kUnsupportedVersion);
}

TEST_F(PacketMetadataTest, UdpLenMismatch) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kUdpLenMismatchPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kUdpLengthMismatch);
}

TEST_F(PacketMetadataTest, VxlanTruncated) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kVxlanTruncatedPacket, alloc_);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kTooShort);
}

TEST_F(PacketMetadataTest, BadChecksum) {
  using namespace rxtx;
  using namespace rxtx::testing;
  HexPacket pkt(kBadChecksumPacket, alloc_, 0, RTE_MBUF_F_RX_IP_CKSUM_BAD);
  ASSERT_TRUE(pkt.Valid());
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt.GetPacket(), meta);
  EXPECT_EQ(result, ParseResult::kChecksumError);
}

// ============================================================================
// Property-based tests (rapidcheck)
// ============================================================================

// Feature: parser-and-test-builder-enhancement, Property 1: Fragmentation flag correctness
// **Validates: Requirements 1.1, 1.4, 1.6, 3.2**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, FragmentationFlagCorrectnessIpv4,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto frag_offset = *rc::gen::inRange(0, 8192);
  auto mf_flag = *rc::gen::arbitrary<bool>();

  auto* mbuf = TestPacketBuilder(alloc_)
      .SetSrcIpv4(0x0A000001)
      .SetDstIpv4(0x0A000002)
      .SetSrcPort(1234)
      .SetDstPort(80)
      .SetFragOffset(static_cast<uint16_t>(frag_offset))
      .SetMfFlag(mf_flag)
      .BuildIpv4Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  bool is_fragmented = (frag_offset != 0 || mf_flag);
  if (is_fragmented) {
    RC_ASSERT(meta.IsFragment());
    RC_ASSERT(meta.frag_offset == static_cast<uint16_t>(frag_offset));
  } else {
    RC_ASSERT(!meta.IsFragment());
    RC_ASSERT(meta.frag_offset == 0);
  }
}

// Feature: parser-and-test-builder-enhancement, Property 1: Fragmentation flag correctness (IPv6)
// **Validates: Requirements 3.2**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, FragmentationFlagCorrectnessIpv6,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto frag_offset = *rc::gen::inRange(0, 8192);
  auto mf = *rc::gen::arbitrary<bool>();
  auto frag_id = *rc::gen::arbitrary<uint32_t>();

  auto* mbuf = TestPacketBuilder(alloc_)
      .SetSrcPort(5678)
      .SetDstPort(443)
      .AddIpv6FragmentHdr(static_cast<uint16_t>(frag_offset), mf, frag_id)
      .BuildIpv6Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  bool is_fragmented = (frag_offset != 0 || mf);
  if (is_fragmented) {
    RC_ASSERT(meta.IsFragment());
    RC_ASSERT(meta.frag_offset == static_cast<uint16_t>(frag_offset));
  } else {
    RC_ASSERT(!meta.IsFragment());
    RC_ASSERT(meta.frag_offset == 0);
  }
}

// Feature: parser-and-test-builder-enhancement, Property 2: Non-first fragment port zeroing
// **Validates: Requirements 1.2, 1.3, 3.3, 3.4**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, NonFirstFragmentPortZeroingIpv4,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto frag_offset = *rc::gen::inRange(1, 8192);
  auto src_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto dst_port = *rc::gen::inRange<uint16_t>(1, 65535);

  auto* mbuf = TestPacketBuilder(alloc_)
      .SetSrcIpv4(0x0A000001)
      .SetDstIpv4(0x0A000002)
      .SetSrcPort(src_port)
      .SetDstPort(dst_port)
      .SetFragOffset(static_cast<uint16_t>(frag_offset))
      .BuildIpv4Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // Non-first fragment: ports and l4_len must be zeroed.
  RC_ASSERT(meta.src_port == 0);
  RC_ASSERT(meta.dst_port == 0);
  RC_ASSERT(pkt.Mbuf()->l4_len == (unsigned char)0);
}

// Feature: parser-and-test-builder-enhancement, Property 2: Non-first fragment port zeroing (IPv6)
// **Validates: Requirements 3.3, 3.4**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, NonFirstFragmentPortZeroingIpv6,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto frag_offset = *rc::gen::inRange(1, 8192);
  auto src_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto dst_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto frag_id = *rc::gen::arbitrary<uint32_t>();

  auto* mbuf = TestPacketBuilder(alloc_)
      .SetSrcPort(src_port)
      .SetDstPort(dst_port)
      .AddIpv6FragmentHdr(static_cast<uint16_t>(frag_offset), false, frag_id)
      .BuildIpv6Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // Non-first fragment: ports and l4_len must be zeroed.
  RC_ASSERT(meta.src_port == 0);
  RC_ASSERT(meta.dst_port == 0);
  RC_ASSERT(pkt.Mbuf()->l4_len == (unsigned char)0);
}

// Feature: parser-and-test-builder-enhancement, Property 2: Non-first fragment port zeroing (first/unfragmented)
// **Validates: Requirements 1.3, 3.4**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, FirstFragmentAndUnfragmentedPortsNormal,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto src_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto dst_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto is_first_fragment = *rc::gen::arbitrary<bool>();

  // frag_offset=0 with mf=true (first fragment) or mf=false (unfragmented).
  auto* mbuf = TestPacketBuilder(alloc_)
      .SetSrcIpv4(0x0A000001)
      .SetDstIpv4(0x0A000002)
      .SetSrcPort(src_port)
      .SetDstPort(dst_port)
      .SetFragOffset(0)
      .SetMfFlag(is_first_fragment)
      .BuildIpv4Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // First fragment or unfragmented: ports must match builder values, l4_len > 0.
  RC_ASSERT(meta.src_port == src_port);
  RC_ASSERT(meta.dst_port == dst_port);
  RC_ASSERT(pkt.Mbuf()->l4_len > (unsigned char)0);
}

// Feature: parser-and-test-builder-enhancement, Property 3: IPv4 options flag and L4 offset correctness
// **Validates: Requirements 2.1, 2.2, 2.3**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, Ipv4OptionsFlagAndL4OffsetCorrectness,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto ihl = *rc::gen::inRange(5, 16);
  auto src_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto dst_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto proto = *rc::gen::element(6, 17);

  auto builder = TestPacketBuilder(alloc_)
      .SetSrcIpv4(0x0A000001)
      .SetDstIpv4(0x0A000002)
      .SetSrcPort(src_port)
      .SetDstPort(dst_port)
      .SetIhl(static_cast<uint8_t>(ihl));

  rte_mbuf* mbuf = (proto == 6) ? builder.BuildIpv4Tcp()
                                : builder.BuildIpv4Udp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // IPv4 options flag set iff IHL > 5.
  if (ihl > 5) {
    RC_ASSERT(meta.HasIpv4Options());
  } else {
    RC_ASSERT(!meta.HasIpv4Options());
  }

  // Ports extracted correctly regardless of options presence.
  RC_ASSERT(meta.src_port == src_port);
  RC_ASSERT(meta.dst_port == dst_port);

  // L3 length accounts for options.
  RC_ASSERT(pkt.Mbuf()->l3_len == static_cast<uint16_t>(ihl * 4));
}

// Feature: parser-and-test-builder-enhancement, Property 4: IPv6 extension header chain walking
// **Validates: Requirements 3.1, 3.5, 3.7, 3.8, 3.9**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, Ipv6ExtHeaderChainWalking,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  // Generate 1–3 generic extension headers from {Routing(43), DstOpts(60)}.
  auto num_ext_hdrs = *rc::gen::inRange(1, 4);
  auto src_port = *rc::gen::inRange<uint16_t>(1, 65535);
  auto dst_port = *rc::gen::inRange<uint16_t>(1, 65535);

  auto builder = TestPacketBuilder(alloc_)
      .SetSrcPort(src_port)
      .SetDstPort(dst_port);

  for (int i = 0; i < num_ext_hdrs; ++i) {
    // Randomly choose Routing (43) or Destination Options (60).
    auto ext_type = *rc::gen::element<uint8_t>((uint8_t)43, (uint8_t)60);
    builder.AddIpv6ExtHdr(ext_type, 0);  // len_units=0 → 8 bytes each
  }

  auto* mbuf = builder.BuildIpv6Tcp();
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // Extension headers flag must be set.
  RC_ASSERT(meta.HasIpv6ExtHeaders());
  // Protocol should be TCP (6) after walking the chain.
  RC_ASSERT(meta.protocol == IPPROTO_TCP);
  // l3_len = 40 (IPv6 fixed) + num_ext_hdrs * 8 (each ext hdr is 8 bytes with len_units=0).
  RC_ASSERT(pkt.Mbuf()->l3_len == static_cast<uint16_t>(40 + num_ext_hdrs * 8));
  // Ports must match builder values.
  RC_ASSERT(meta.src_port == src_port);
  RC_ASSERT(meta.dst_port == dst_port);
}

// Feature: parser-and-test-builder-enhancement, Property 4: IPv6 extension header chain walking (AH/ESP termination)
// **Validates: Requirements 3.8, 3.9**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, Ipv6ExtHeaderAhEspTermination,
                       ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  // Generate 0–2 generic extension headers before the terminal AH/ESP.
  auto num_ext_hdrs = *rc::gen::inRange(0, 3);
  // Terminal protocol: AH (51) or ESP (50).
  auto terminal_proto = *rc::gen::element<uint8_t>((uint8_t)51, (uint8_t)50);

  auto builder = TestPacketBuilder(alloc_)
      .SetSrcPort(9999)
      .SetDstPort(8888);

  for (int i = 0; i < num_ext_hdrs; ++i) {
    auto ext_type = *rc::gen::element<uint8_t>((uint8_t)43, (uint8_t)60);
    builder.AddIpv6ExtHdr(ext_type, 0);
  }

  // Build with AH or ESP as the L4 protocol. The last ext header's next_hdr
  // will point to terminal_proto, and the parser will stop walking there.
  auto* mbuf = builder.BuildIpv6(terminal_proto);
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  // Protocol should be AH or ESP.
  RC_ASSERT(meta.protocol == terminal_proto);
  // AH/ESP → ports must be zero.
  RC_ASSERT(meta.src_port == 0);
  RC_ASSERT(meta.dst_port == 0);
  // l4_len must be zero for AH/ESP.
  RC_ASSERT(pkt.Mbuf()->l4_len == (uint8_t)0);
  // l3_len = 40 + num_ext_hdrs * 8.
  RC_ASSERT(pkt.Mbuf()->l3_len == static_cast<uint16_t>(40 + num_ext_hdrs * 8));
  // If any ext headers were present, the flag should be set.
  // Note: AH/ESP themselves are recognized as ext headers by IsIpv6ExtensionHeader(),
  // so has_ext is always true (the parser enters the while loop for AH/ESP).
  RC_ASSERT(meta.HasIpv6ExtHeaders());
}

// Feature: parser-and-test-builder-enhancement, Property 5: VLAN metadata extraction
// **Validates: Requirements 4.1, 4.2, 4.3, 4.5**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, VlanMetadataExtraction, ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto vlan_mode = *rc::gen::inRange(0, 3);
  auto outer_vid = *rc::gen::inRange(0, 4096);
  auto inner_vid = *rc::gen::inRange(0, 4096);

  rte_mbuf* mbuf = nullptr;
  if (vlan_mode == 0) {
    // No VLAN tags — plain Ethernet.
    mbuf = TestPacketBuilder(alloc_)
        .SetSrcIpv4(0x0A000001)
        .SetDstIpv4(0x0A000002)
        .SetSrcPort(1234)
        .SetDstPort(80)
        .BuildIpv4Tcp();
  } else if (vlan_mode == 1) {
    // Single VLAN tag (802.1Q).
    mbuf = TestPacketBuilder(alloc_)
        .SetSrcIpv4(0x0A000001)
        .SetDstIpv4(0x0A000002)
        .SetSrcPort(1234)
        .SetDstPort(80)
        .SetVlanId(static_cast<uint16_t>(outer_vid))
        .BuildIpv4Tcp();
  } else {
    // QinQ double VLAN (802.1ad + 802.1Q).
    mbuf = TestPacketBuilder(alloc_)
        .SetSrcIpv4(0x0A000001)
        .SetDstIpv4(0x0A000002)
        .SetSrcPort(1234)
        .SetDstPort(80)
        .SetOuterVlanId(static_cast<uint16_t>(outer_vid))
        .SetInnerVlanId(static_cast<uint16_t>(inner_vid))
        .BuildIpv4Tcp();
  }
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  if (vlan_mode == 0) {
    RC_ASSERT(meta.vlan_count == 0);
    RC_ASSERT(meta.outer_vlan_id == 0);
    RC_ASSERT(meta.inner_vlan_id == 0);
    RC_ASSERT(pkt.Mbuf()->l2_len == (uint8_t)14);
  } else if (vlan_mode == 1) {
    RC_ASSERT(meta.vlan_count == 1);
    RC_ASSERT(meta.outer_vlan_id == static_cast<uint16_t>(outer_vid));
    RC_ASSERT(meta.inner_vlan_id == 0);
    RC_ASSERT(pkt.Mbuf()->l2_len == (uint8_t)18);
  } else {
    RC_ASSERT(meta.vlan_count == 2);
    RC_ASSERT(meta.outer_vlan_id == static_cast<uint16_t>(outer_vid));
    RC_ASSERT(meta.inner_vlan_id == static_cast<uint16_t>(inner_vid));
    RC_ASSERT(pkt.Mbuf()->l2_len == (uint8_t)22);
  }
}

// Feature: parser-and-test-builder-enhancement, Property 6: HexPacket decode/encode round-trip
// **Validates: Requirements 6.1, 7.4**
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, HexPacketDecodeEncodeRoundTrip, ()) {
  using namespace rxtx::testing;

  // Generate a random number of bytes (1–100).
  auto num_bytes = *rc::gen::inRange(1, 101);

  // Generate that many random bytes.
  auto bytes = *rc::gen::container<std::vector<uint8_t>>(
      num_bytes, rc::gen::inRange(0, 256));

  // Encode the bytes to a lowercase hex string.
  auto to_hex = [](const uint8_t* data, uint16_t len) -> std::string {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (uint16_t i = 0; i < len; ++i) {
      result += hex_chars[(data[i] >> 4) & 0x0F];
      result += hex_chars[data[i] & 0x0F];
    }
    return result;
  };

  std::string hex_input = to_hex(bytes.data(), static_cast<uint16_t>(bytes.size()));

  // Decode via HexPacket.
  HexPacket pkt(hex_input.c_str(), alloc_);
  RC_ASSERT(pkt.Valid());

  // Read back the mbuf data bytes and re-encode to hex.
  const uint8_t* mbuf_data = rte_pktmbuf_mtod(pkt.GetPacket().Mbuf(), const uint8_t*);
  std::string hex_output = to_hex(mbuf_data, pkt.Length());

  // Round-trip: re-encoded hex must match the original.
  RC_ASSERT(hex_output == hex_input);
}

// Feature: parser-and-test-builder-enhancement, Property 7: ICMP/ICMPv6 field extraction
// **Validates: Requirements 9.1, 9.2, 9.3, 9.4, 9.5, 9.6**

// Test 1 — IPv4 ICMP
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, IcmpFieldExtraction, ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto icmp_type = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  auto icmp_code = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  auto identifier = *rc::gen::arbitrary<uint16_t>();

  rte_mbuf* mbuf = TestPacketBuilder(alloc_)
      .SetSrcIpv4(0x0A000001)
      .SetDstIpv4(0x0A000002)
      .BuildIpv4Icmp(icmp_type, icmp_code, identifier);
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  RC_ASSERT(meta.protocol == IPPROTO_ICMP);
  RC_ASSERT(meta.dst_port == ((static_cast<uint16_t>(icmp_type) << 8) | icmp_code));

  bool is_echo = (icmp_type == 8 || icmp_type == 0);
  if (is_echo) {
    RC_ASSERT(meta.src_port == identifier);
  } else {
    RC_ASSERT(meta.src_port == 0);
  }

  RC_ASSERT(pkt.Mbuf()->l4_len == (uint8_t)8);
}

// Test 2 — IPv6 ICMPv6
RC_GTEST_FIXTURE_PROP(PacketMetadataTest, Icmpv6FieldExtraction, ()) {
  using namespace rxtx;
  using namespace rxtx::testing;

  auto icmp_type = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  auto icmp_code = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  auto identifier = *rc::gen::arbitrary<uint16_t>();

  rte_mbuf* mbuf = TestPacketBuilder(alloc_)
      .BuildIpv6Icmp(icmp_type, icmp_code, identifier);
  RC_ASSERT(mbuf != nullptr);

  Packet& pkt = Packet::from(mbuf);
  PacketMetadata meta{};
  auto result = PacketMetadata::Parse(pkt, meta);
  RC_ASSERT(result == ParseResult::kOk);

  RC_ASSERT(meta.protocol == IPPROTO_ICMPV6);
  RC_ASSERT(meta.dst_port == ((static_cast<uint16_t>(icmp_type) << 8) | icmp_code));

  bool is_echo = (icmp_type == 128 || icmp_type == 129);
  if (is_echo) {
    RC_ASSERT(meta.src_port == identifier);
  } else {
    RC_ASSERT(meta.src_port == 0);
  }

  RC_ASSERT(pkt.Mbuf()->l4_len == (uint8_t)8);
  RC_ASSERT(meta.IsIpv6());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
