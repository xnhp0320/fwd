// rxtx/test_packet_builder.h
// Test helper: fluent builder for constructing raw packet bytes in an mbuf.
// Used by property-based tests (rapidcheck) that generate random packet
// parameters and verify Parse produces correct results.
#ifndef RXTX_TEST_PACKET_BUILDER_H_
#define RXTX_TEST_PACKET_BUILDER_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_vxlan.h>

#include "rxtx/test_utils.h"

namespace rxtx {
namespace testing {

// Builds raw packet bytes directly in an mbuf data area.
// Supports Ethernet + IPv4/IPv6 + TCP/UDP, VXLAN encapsulation,
// VLAN tags, and injection of malformed fields for error-path testing.
//
// Usage:
//   auto* mbuf = TestPacketBuilder(alloc)
//       .SetSrcIpv4(0x0A000001)
//       .SetDstIpv4(0x0A000002)
//       .SetSrcPort(1234)
//       .SetDstPort(80)
//       .SetProtocol(6)
//       .BuildIpv4Tcp();
class TestPacketBuilder {
 public:
  explicit TestPacketBuilder(TestMbufAllocator& alloc) : alloc_(alloc) {}

  // --- Inner (or only) layer fields ---

  TestPacketBuilder& SetSrcIpv4(uint32_t ip) {
    src_ipv4_ = ip;
    return *this;
  }
  TestPacketBuilder& SetDstIpv4(uint32_t ip) {
    dst_ipv4_ = ip;
    return *this;
  }
  TestPacketBuilder& SetSrcIpv6(const uint8_t ip[16]) {
    std::memcpy(src_ipv6_, ip, 16);
    return *this;
  }
  TestPacketBuilder& SetDstIpv6(const uint8_t ip[16]) {
    std::memcpy(dst_ipv6_, ip, 16);
    return *this;
  }

  TestPacketBuilder& SetSrcPort(uint16_t port) {
    src_port_ = port;
    return *this;
  }
  TestPacketBuilder& SetDstPort(uint16_t port) {
    dst_port_ = port;
    return *this;
  }
  TestPacketBuilder& SetProtocol(uint8_t proto) {
    protocol_ = proto;
    return *this;
  }

  // --- Outer layer fields (for VXLAN) ---

  TestPacketBuilder& SetOuterSrcIpv4(uint32_t ip) {
    outer_src_ipv4_ = ip;
    return *this;
  }
  TestPacketBuilder& SetOuterDstIpv4(uint32_t ip) {
    outer_dst_ipv4_ = ip;
    return *this;
  }
  TestPacketBuilder& SetOuterSrcPort(uint16_t port) {
    outer_src_port_ = port;
    return *this;
  }
  TestPacketBuilder& SetVni(uint32_t vni) {
    vni_ = vni;
    return *this;
  }

  // --- VLAN ---

  TestPacketBuilder& SetVlanId(uint16_t vid) {
    vlan_id_ = vid;
    has_vlan_ = true;
    return *this;
  }

  // --- QinQ (double VLAN) ---

  TestPacketBuilder& SetOuterVlanId(uint16_t vid) {
    outer_vlan_id_ = vid;
    has_outer_vlan_ = true;
    return *this;
  }
  TestPacketBuilder& SetInnerVlanId(uint16_t vid) {
    inner_vlan_id_ = vid;
    has_inner_vlan_ = true;
    return *this;
  }

  // --- IPv4 fragmentation ---

  TestPacketBuilder& SetFragOffset(uint16_t offset) {
    frag_offset_ = offset;
    return *this;
  }
  TestPacketBuilder& SetMfFlag(bool mf) {
    mf_flag_ = mf;
    return *this;
  }

  // --- IPv4 options (IHL > 5) ---

  // Set IHL value (5–15). When > 5, (ihl - 5) * 4 NOP padding bytes are
  // written after the standard 20-byte IPv4 header.
  TestPacketBuilder& SetIhl(uint8_t ihl) {
    ihl_ = ihl;
    return *this;
  }

  // --- IPv6 extension headers ---

  // Add a generic IPv6 extension header (Hop-by-Hop=0, Routing=43,
  // Destination Options=60). len_units is the "Header Ext Len" field;
  // the header occupies (len_units + 1) * 8 bytes.
  TestPacketBuilder& AddIpv6ExtHdr(uint8_t type, uint8_t len_units) {
    ipv6_ext_hdrs_.push_back({type, len_units, 0, false, 0});
    return *this;
  }

  // Add an IPv6 Fragment header (always 8 bytes).
  TestPacketBuilder& AddIpv6FragmentHdr(uint16_t frag_offset, bool mf,
                                        uint32_t id) {
    ipv6_ext_hdrs_.push_back({44, 0, frag_offset, mf, id});
    return *this;
  }

  // --- Hardware classification simulation ---

  TestPacketBuilder& SetPacketType(uint32_t ptype) {
    packet_type_ = ptype;
    has_packet_type_ = true;
    return *this;
  }
  TestPacketBuilder& SetOlFlags(uint64_t flags) {
    ol_flags_ = flags;
    has_ol_flags_ = true;
    return *this;
  }

  // --- Malformed field injection ---

  // Override the mbuf data_len after building (for truncated packet tests).
  TestPacketBuilder& SetOverrideDataLen(uint16_t len) {
    override_data_len_ = len;
    has_override_data_len_ = true;
    return *this;
  }
  // Override IPv4 IHL field (normally auto-computed as 5).
  TestPacketBuilder& SetOverrideIhl(uint8_t ihl) {
    override_ihl_ = ihl;
    has_override_ihl_ = true;
    return *this;
  }
  // Override IP version nibble (normally 4 or 6).
  TestPacketBuilder& SetOverrideIpVersion(uint8_t ver) {
    override_ip_version_ = ver;
    has_override_ip_version_ = true;
    return *this;
  }
  // Override UDP dgram_len field (normally auto-computed).
  TestPacketBuilder& SetOverrideUdpLen(uint16_t len) {
    override_udp_len_ = len;
    has_override_udp_len_ = true;
    return *this;
  }
  // Override IPv4 total_length field (normally auto-computed).
  TestPacketBuilder& SetOverrideIpTotalLen(uint16_t len) {
    override_ip_total_len_ = len;
    has_override_ip_total_len_ = true;
    return *this;
  }
  // Override IPv6 payload_len field (normally auto-computed).
  TestPacketBuilder& SetOverrideIpv6PayloadLen(uint16_t len) {
    override_ipv6_payload_len_ = len;
    has_override_ipv6_payload_len_ = true;
    return *this;
  }

  // --- Build methods ---

  // Build Ethernet + IPv4 + TCP packet.
  rte_mbuf* BuildIpv4Tcp() {
    return BuildIpv4(kProtoTcp);
  }

  // Build Ethernet + IPv4 + UDP packet.
  rte_mbuf* BuildIpv4Udp() {
    return BuildIpv4(kProtoUdp);
  }

  // Build Ethernet + IPv4 + arbitrary protocol (no L4 header for non-TCP/UDP).
  rte_mbuf* BuildIpv4(uint8_t proto) {
    protocol_ = proto;
    uint16_t l2 = EthHdrLen();
    uint16_t ip_hdr_len = static_cast<uint16_t>(ihl_) * 4;
    uint16_t l4 = L4HdrLen(proto);
    uint16_t total = l2 + ip_hdr_len + l4;

    rte_mbuf* m = alloc_.Alloc(RTE_PKTMBUF_HEADROOM, total);
    if (!m) return nullptr;
    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    std::memset(data, 0, total);

    uint16_t offset = 0;
    offset = WriteEthHdr(data, offset, RTE_ETHER_TYPE_IPV4);
    offset = WriteIpv4Hdr(data, offset, ip_hdr_len + l4);
    // If IHL > 5, fill options area with NOP (0x01) padding.
    if (ihl_ > 5) {
      uint16_t opts_len = (ihl_ - 5) * 4;
      std::memset(data + offset, 0x01, opts_len);
      offset += opts_len;
    }
    WriteL4Hdr(data, offset, proto, l4);

    ApplyOverrides(m, data, l2);
    return m;
  }

  // Build Ethernet + IPv6 + TCP packet.
  rte_mbuf* BuildIpv6Tcp() {
    return BuildIpv6(kProtoTcp);
  }

  // Build Ethernet + IPv6 + UDP packet.
  rte_mbuf* BuildIpv6Udp() {
    return BuildIpv6(kProtoUdp);
  }

  // Build Ethernet + IPv6 + arbitrary protocol.
  rte_mbuf* BuildIpv6(uint8_t proto) {
    protocol_ = proto;
    uint16_t l2 = EthHdrLen();
    uint16_t l3 = kIpv6HdrLen;
    uint16_t ext_len = Ipv6ExtHdrsTotalLen();
    uint16_t l4 = L4HdrLen(proto);
    uint16_t total = l2 + l3 + ext_len + l4;

    rte_mbuf* m = alloc_.Alloc(RTE_PKTMBUF_HEADROOM, total);
    if (!m) return nullptr;
    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    std::memset(data, 0, total);

    uint16_t offset = 0;
    offset = WriteEthHdr(data, offset, RTE_ETHER_TYPE_IPV6);
    offset = WriteIpv6Hdr(data, offset, ext_len + l4);
    offset = WriteIpv6ExtHdrs(data, offset, proto);
    WriteL4Hdr(data, offset, proto, l4);

    ApplyOverrides(m, data, l2);
    return m;
  }

  // Build Ethernet + IPv4 + ICMP packet.
  rte_mbuf* BuildIpv4Icmp(uint8_t type, uint8_t code, uint16_t identifier) {
    icmp_type_ = type;
    icmp_code_ = code;
    icmp_identifier_ = identifier;
    return BuildIpv4(kProtoIcmp);
  }

  // Build Ethernet + IPv6 + ICMPv6 packet.
  rte_mbuf* BuildIpv6Icmp(uint8_t type, uint8_t code, uint16_t identifier) {
    icmp_type_ = type;
    icmp_code_ = code;
    icmp_identifier_ = identifier;
    return BuildIpv6(kProtoIcmpv6);
  }

  // Build VXLAN: outer Ethernet + outer IPv4 + outer UDP + VXLAN hdr +
  //              inner Ethernet + inner IPv4 + inner TCP/UDP.
  rte_mbuf* BuildVxlanIpv4Tcp() {
    return BuildVxlan(false, kProtoTcp);
  }
  rte_mbuf* BuildVxlanIpv4Udp() {
    return BuildVxlan(false, kProtoUdp);
  }
  rte_mbuf* BuildVxlanIpv6Tcp() {
    return BuildVxlan(true, kProtoTcp);
  }
  rte_mbuf* BuildVxlanIpv6Udp() {
    return BuildVxlan(true, kProtoUdp);
  }

  // Build VXLAN with specified inner IP version and L4 protocol.
  rte_mbuf* BuildVxlan(bool inner_ipv6, uint8_t inner_proto) {
    protocol_ = inner_proto;

    // Outer: Ethernet + IPv4 + UDP (always IPv4 outer for simplicity).
    uint16_t outer_l2 = kEthHdrLen;  // No VLAN on outer for now.
    uint16_t outer_l3 = kIpv4MinHdrLen;
    uint16_t outer_l4 = kUdpHdrLen;
    uint16_t vxlan_hdr = kVxlanHdrLen;

    // Inner: Ethernet + IP + L4.
    uint16_t inner_l2 = kEthHdrLen;
    uint16_t inner_l3 = inner_ipv6 ? kIpv6HdrLen : kIpv4MinHdrLen;
    uint16_t inner_l4 = L4HdrLen(inner_proto);

    uint16_t total = outer_l2 + outer_l3 + outer_l4 + vxlan_hdr +
                     inner_l2 + inner_l3 + inner_l4;

    rte_mbuf* m = alloc_.Alloc(RTE_PKTMBUF_HEADROOM, total);
    if (!m) return nullptr;
    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    std::memset(data, 0, total);

    uint16_t offset = 0;

    // Outer Ethernet.
    auto* eth = reinterpret_cast<rte_ether_hdr*>(data + offset);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    offset += outer_l2;

    // Outer IPv4.
    uint16_t outer_ip_payload = outer_l4 + vxlan_hdr + inner_l2 + inner_l3 + inner_l4;
    auto* ipv4 = reinterpret_cast<rte_ipv4_hdr*>(data + offset);
    ipv4->version_ihl = (4 << 4) | 5;
    ipv4->total_length = rte_cpu_to_be_16(outer_l3 + outer_ip_payload);
    ipv4->next_proto_id = kProtoUdp;
    ipv4->src_addr = rte_cpu_to_be_32(outer_src_ipv4_);
    ipv4->dst_addr = rte_cpu_to_be_32(outer_dst_ipv4_);
    offset += outer_l3;

    // Outer UDP (dst port = 4789 for VXLAN).
    auto* udp = reinterpret_cast<rte_udp_hdr*>(data + offset);
    udp->src_port = rte_cpu_to_be_16(outer_src_port_);
    udp->dst_port = rte_cpu_to_be_16(kVxlanPort);
    udp->dgram_len = rte_cpu_to_be_16(outer_l4 + vxlan_hdr +
                                       inner_l2 + inner_l3 + inner_l4);
    offset += outer_l4;

    // VXLAN header.
    auto* vxlan = reinterpret_cast<rte_vxlan_hdr*>(data + offset);
    // Set I-flag (bit 3 of first byte) to indicate valid VNI.
    data[offset] = 0x08;
    vxlan->vni[0] = static_cast<uint8_t>((vni_ >> 16) & 0xFF);
    vxlan->vni[1] = static_cast<uint8_t>((vni_ >> 8) & 0xFF);
    vxlan->vni[2] = static_cast<uint8_t>(vni_ & 0xFF);
    offset += vxlan_hdr;

    // Inner Ethernet.
    uint16_t inner_ether_type = inner_ipv6 ? RTE_ETHER_TYPE_IPV6
                                           : RTE_ETHER_TYPE_IPV4;
    auto* inner_eth = reinterpret_cast<rte_ether_hdr*>(data + offset);
    inner_eth->ether_type = rte_cpu_to_be_16(inner_ether_type);
    offset += inner_l2;

    // Inner IP + L4.
    if (inner_ipv6) {
      offset = WriteIpv6Hdr(data, offset, inner_l4);
    } else {
      offset = WriteIpv4Hdr(data, offset, inner_l3 + inner_l4);
    }
    WriteL4Hdr(data, offset, inner_proto, inner_l4);

    ApplyOverrides(m, data, outer_l2);
    return m;
  }

 private:
  static constexpr uint8_t kProtoTcp = 6;
  static constexpr uint8_t kProtoUdp = 17;
  static constexpr uint8_t kProtoIcmp = 1;
  static constexpr uint8_t kProtoIcmpv6 = 58;
  static constexpr uint16_t kVxlanPort = 4789;
  static constexpr uint16_t kEthHdrLen = RTE_ETHER_HDR_LEN;       // 14
  static constexpr uint16_t kVlanHdrLen = sizeof(rte_vlan_hdr);    // 4
  static constexpr uint16_t kIpv4MinHdrLen = 20;
  static constexpr uint16_t kIpv6HdrLen = 40;
  static constexpr uint16_t kTcpMinHdrLen = 20;
  static constexpr uint16_t kUdpHdrLen = sizeof(rte_udp_hdr);     // 8
  static constexpr uint16_t kIcmpHdrLen = 8;
  static constexpr uint16_t kVxlanHdrLen = 8;
  static constexpr uint16_t kQinQTagEtherType = 0x88A8;

  // Ethernet header length including optional VLAN tag(s).
  uint16_t EthHdrLen() const {
    if (has_outer_vlan_ && has_inner_vlan_) {
      return kEthHdrLen + kVlanHdrLen + kVlanHdrLen;  // QinQ: 14 + 4 + 4 = 22
    }
    if (has_vlan_ || has_outer_vlan_) {
      return kEthHdrLen + kVlanHdrLen;  // Single VLAN: 14 + 4 = 18
    }
    return kEthHdrLen;
  }

  // L4 header length for a given protocol.
  static uint16_t L4HdrLen(uint8_t proto) {
    if (proto == kProtoTcp) return kTcpMinHdrLen;
    if (proto == kProtoUdp) return kUdpHdrLen;
    if (proto == kProtoIcmp || proto == kProtoIcmpv6) return kIcmpHdrLen;
    return 0;  // No L4 header for other protocols.
  }

  // Write Ethernet header (with optional VLAN/QinQ) at offset. Returns new offset.
  uint16_t WriteEthHdr(uint8_t* data, uint16_t offset,
                       uint16_t ether_type) {
    auto* eth = reinterpret_cast<rte_ether_hdr*>(data + offset);
    if (has_outer_vlan_ && has_inner_vlan_) {
      // QinQ: EtherType=0x88A8, outer VLAN, 0x8100, inner VLAN, IP EtherType
      eth->ether_type = rte_cpu_to_be_16(kQinQTagEtherType);
      offset += kEthHdrLen;
      auto* outer_vlan = reinterpret_cast<rte_vlan_hdr*>(data + offset);
      outer_vlan->vlan_tci = rte_cpu_to_be_16(outer_vlan_id_);
      outer_vlan->eth_proto = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);
      offset += kVlanHdrLen;
      auto* inner_vlan = reinterpret_cast<rte_vlan_hdr*>(data + offset);
      inner_vlan->vlan_tci = rte_cpu_to_be_16(inner_vlan_id_);
      inner_vlan->eth_proto = rte_cpu_to_be_16(ether_type);
      offset += kVlanHdrLen;
    } else if (has_vlan_) {
      eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);
      offset += kEthHdrLen;
      auto* vlan = reinterpret_cast<rte_vlan_hdr*>(data + offset);
      vlan->vlan_tci = rte_cpu_to_be_16(vlan_id_);
      vlan->eth_proto = rte_cpu_to_be_16(ether_type);
      offset += kVlanHdrLen;
    } else if (has_outer_vlan_) {
      // Single outer VLAN without inner (treated as single 802.1Q tag).
      eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);
      offset += kEthHdrLen;
      auto* vlan = reinterpret_cast<rte_vlan_hdr*>(data + offset);
      vlan->vlan_tci = rte_cpu_to_be_16(outer_vlan_id_);
      vlan->eth_proto = rte_cpu_to_be_16(ether_type);
      offset += kVlanHdrLen;
    } else {
      eth->ether_type = rte_cpu_to_be_16(ether_type);
      offset += kEthHdrLen;
    }
    return offset;
  }

  // Write IPv4 header at offset. ip_total_len includes IP header + L4 + payload.
  // Returns new offset past the standard 20-byte IPv4 header (options written
  // separately by the caller).
  uint16_t WriteIpv4Hdr(uint8_t* data, uint16_t offset,
                        uint16_t ip_total_len) {
    auto* ipv4 = reinterpret_cast<rte_ipv4_hdr*>(data + offset);
    ipv4->version_ihl = (4 << 4) | (ihl_ & 0x0F);
    ipv4->total_length = rte_cpu_to_be_16(ip_total_len);
    ipv4->next_proto_id = protocol_;
    ipv4->src_addr = rte_cpu_to_be_32(src_ipv4_);
    ipv4->dst_addr = rte_cpu_to_be_32(dst_ipv4_);
    // Fragment offset and MF flag.
    uint16_t frag_field = frag_offset_ & 0x1FFF;
    if (mf_flag_) {
      frag_field |= 0x2000;  // RTE_IPV4_HDR_MF_FLAG
    }
    ipv4->fragment_offset = rte_cpu_to_be_16(frag_field);
    return offset + kIpv4MinHdrLen;
  }

  // Write IPv6 header at offset. payload_len is the L4 header + payload size
  // (including extension headers).
  // Returns new offset past the IPv6 header.
  uint16_t WriteIpv6Hdr(uint8_t* data, uint16_t offset,
                        uint16_t payload_len) {
    auto* ipv6 = reinterpret_cast<rte_ipv6_hdr*>(data + offset);
    // Version (4 bits) = 6, traffic class = 0, flow label = 0.
    // First byte: (6 << 4) = 0x60.
    data[offset] = 0x60;
    ipv6->payload_len = rte_cpu_to_be_16(payload_len);
    // If extension headers are present, the IPv6 Next Header points to the
    // first extension header type; otherwise it points to the L4 protocol.
    if (!ipv6_ext_hdrs_.empty()) {
      ipv6->proto = ipv6_ext_hdrs_[0].type;
    } else {
      ipv6->proto = protocol_;
    }
    ipv6->hop_limits = 64;
    std::memcpy(ipv6->src_addr.a, src_ipv6_, 16);
    std::memcpy(ipv6->dst_addr.a, dst_ipv6_, 16);
    return offset + kIpv6HdrLen;
  }

  // Write L4 header (TCP, UDP, or ICMP) at offset.
  void WriteL4Hdr(uint8_t* data, uint16_t offset, uint8_t proto,
                  uint16_t l4_segment_len) {
    if (proto == kProtoTcp) {
      auto* tcp = reinterpret_cast<rte_tcp_hdr*>(data + offset);
      tcp->src_port = rte_cpu_to_be_16(src_port_);
      tcp->dst_port = rte_cpu_to_be_16(dst_port_);
      tcp->data_off = (5 << 4);  // 20 bytes, no options.
    } else if (proto == kProtoUdp) {
      auto* udp = reinterpret_cast<rte_udp_hdr*>(data + offset);
      udp->src_port = rte_cpu_to_be_16(src_port_);
      udp->dst_port = rte_cpu_to_be_16(dst_port_);
      udp->dgram_len = rte_cpu_to_be_16(l4_segment_len);
    } else if (proto == kProtoIcmp || proto == kProtoIcmpv6) {
      // ICMP header: type(1) + code(1) + checksum(2) + identifier(2) + sequence(2)
      data[offset + 0] = icmp_type_;
      data[offset + 1] = icmp_code_;
      // Checksum left as 0 (test packets don't need valid checksums).
      data[offset + 2] = 0;
      data[offset + 3] = 0;
      // Identifier (big-endian).
      data[offset + 4] = static_cast<uint8_t>((icmp_identifier_ >> 8) & 0xFF);
      data[offset + 5] = static_cast<uint8_t>(icmp_identifier_ & 0xFF);
      // Sequence number left as 0.
      data[offset + 6] = 0;
      data[offset + 7] = 0;
    }
    // For other protocols, no L4 header is written.
  }

  // Compute total size of all IPv6 extension headers.
  uint16_t Ipv6ExtHdrsTotalLen() const {
    uint16_t total = 0;
    for (const auto& hdr : ipv6_ext_hdrs_) {
      if (hdr.type == 44) {
        total += 8;  // Fragment header is always 8 bytes.
      } else {
        total += (static_cast<uint16_t>(hdr.len_units) + 1) * 8;
      }
    }
    return total;
  }

  // Write IPv6 extension headers at offset. Returns new offset past all
  // extension headers. Each header's Next Header field points to the next
  // extension header type, or to the L4 protocol for the last one.
  uint16_t WriteIpv6ExtHdrs(uint8_t* data, uint16_t offset,
                            uint8_t final_proto) {
    for (size_t i = 0; i < ipv6_ext_hdrs_.size(); ++i) {
      const auto& hdr = ipv6_ext_hdrs_[i];
      // Determine the Next Header value: next ext hdr type, or final L4 proto.
      uint8_t next_hdr = (i + 1 < ipv6_ext_hdrs_.size())
                             ? ipv6_ext_hdrs_[i + 1].type
                             : final_proto;

      if (hdr.type == 44) {
        // Fragment header: next_hdr(1) + reserved(1) + frag_off_mf(2) + id(4)
        data[offset + 0] = next_hdr;
        data[offset + 1] = 0;  // Reserved.
        uint16_t frag_field = static_cast<uint16_t>(hdr.frag_offset << 3);
        if (hdr.mf) frag_field |= 1;
        data[offset + 2] = static_cast<uint8_t>((frag_field >> 8) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>(frag_field & 0xFF);
        data[offset + 4] = static_cast<uint8_t>((hdr.frag_id >> 24) & 0xFF);
        data[offset + 5] = static_cast<uint8_t>((hdr.frag_id >> 16) & 0xFF);
        data[offset + 6] = static_cast<uint8_t>((hdr.frag_id >> 8) & 0xFF);
        data[offset + 7] = static_cast<uint8_t>(hdr.frag_id & 0xFF);
        offset += 8;
      } else {
        // Generic extension header: next_hdr(1) + hdr_ext_len(1) + padding.
        uint16_t hdr_size = (static_cast<uint16_t>(hdr.len_units) + 1) * 8;
        data[offset + 0] = next_hdr;
        data[offset + 1] = hdr.len_units;
        // Remaining bytes are already zeroed (NOP padding).
        offset += hdr_size;
      }
    }
    return offset;
  }

  // Apply overrides for malformed packet injection and hardware simulation.
  void ApplyOverrides(rte_mbuf* m, uint8_t* data, uint16_t l2_len) {
    if (has_packet_type_) {
      m->packet_type = packet_type_;
    }
    if (has_ol_flags_) {
      m->ol_flags = ol_flags_;
    }
    if (has_override_data_len_) {
      m->data_len = override_data_len_;
      m->pkt_len = override_data_len_;
    }
    if (has_override_ihl_) {
      // IHL is in the lower 4 bits of the first byte of the IP header.
      uint8_t* ip_byte = data + l2_len;
      *ip_byte = (*ip_byte & 0xF0) | (override_ihl_ & 0x0F);
    }
    if (has_override_ip_version_) {
      // Version is in the upper 4 bits of the first byte of the IP header.
      uint8_t* ip_byte = data + l2_len;
      *ip_byte = (override_ip_version_ << 4) | (*ip_byte & 0x0F);
    }
    if (has_override_udp_len_) {
      // Find the UDP header: l2 + l3 (assume standard IPv4 20 or IPv6 40).
      // We detect from the version nibble.
      uint8_t ver = (data[l2_len] >> 4) & 0x0F;
      uint16_t l3 = (ver == 6) ? kIpv6HdrLen : kIpv4MinHdrLen;
      uint16_t udp_offset = l2_len + l3;
      auto* udp = reinterpret_cast<rte_udp_hdr*>(data + udp_offset);
      udp->dgram_len = rte_cpu_to_be_16(override_udp_len_);
    }
    if (has_override_ip_total_len_) {
      auto* ipv4 = reinterpret_cast<rte_ipv4_hdr*>(data + l2_len);
      ipv4->total_length = rte_cpu_to_be_16(override_ip_total_len_);
    }
    if (has_override_ipv6_payload_len_) {
      auto* ipv6 = reinterpret_cast<rte_ipv6_hdr*>(data + l2_len);
      ipv6->payload_len = rte_cpu_to_be_16(override_ipv6_payload_len_);
    }
  }

  // --- State ---
  TestMbufAllocator& alloc_;

  // Inner / only layer.
  uint32_t src_ipv4_ = 0;
  uint32_t dst_ipv4_ = 0;
  uint8_t src_ipv6_[16] = {};
  uint8_t dst_ipv6_[16] = {};
  uint16_t src_port_ = 0;
  uint16_t dst_port_ = 0;
  uint8_t protocol_ = 0;

  // Outer layer (VXLAN).
  uint32_t outer_src_ipv4_ = 0;
  uint32_t outer_dst_ipv4_ = 0;
  uint16_t outer_src_port_ = 0;
  uint32_t vni_ = 0;

  // VLAN (single tag via SetVlanId).
  uint16_t vlan_id_ = 0;
  bool has_vlan_ = false;

  // QinQ (double VLAN via SetOuterVlanId / SetInnerVlanId).
  uint16_t outer_vlan_id_ = 0;
  bool has_outer_vlan_ = false;
  uint16_t inner_vlan_id_ = 0;
  bool has_inner_vlan_ = false;

  // IPv4 fragmentation.
  uint16_t frag_offset_ = 0;
  bool mf_flag_ = false;

  // IPv4 IHL (default 5 = no options).
  uint8_t ihl_ = 5;

  // IPv6 extension header descriptor.
  struct Ipv6ExtHdrDesc {
    uint8_t type;          // Extension header type (0, 43, 44, 60).
    uint8_t len_units;     // Header Ext Len field (for non-fragment).
    uint16_t frag_offset;  // Fragment offset (for type 44).
    bool mf;               // More Fragments flag (for type 44).
    uint32_t frag_id;      // Identification (for type 44).
  };
  std::vector<Ipv6ExtHdrDesc> ipv6_ext_hdrs_;

  // ICMP fields.
  uint8_t icmp_type_ = 0;
  uint8_t icmp_code_ = 0;
  uint16_t icmp_identifier_ = 0;

  // Hardware classification simulation.
  uint32_t packet_type_ = 0;
  bool has_packet_type_ = false;
  uint64_t ol_flags_ = 0;
  bool has_ol_flags_ = false;

  // Malformed field overrides.
  uint16_t override_data_len_ = 0;
  bool has_override_data_len_ = false;
  uint8_t override_ihl_ = 0;
  bool has_override_ihl_ = false;
  uint8_t override_ip_version_ = 0;
  bool has_override_ip_version_ = false;
  uint16_t override_udp_len_ = 0;
  bool has_override_udp_len_ = false;
  uint16_t override_ip_total_len_ = 0;
  bool has_override_ip_total_len_ = false;
  uint16_t override_ipv6_payload_len_ = 0;
  bool has_override_ipv6_payload_len_ = false;
};

}  // namespace testing
}  // namespace rxtx

#endif  // RXTX_TEST_PACKET_BUILDER_H_
