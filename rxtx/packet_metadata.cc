// rxtx/packet_metadata.cc
#include "rxtx/packet_metadata.h"

#include <cstring>

#include <netinet/in.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_mbuf_ptype.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_vxlan.h>

#include "rxtx/packet.h"

namespace rxtx {
namespace {

// VXLAN UDP destination port.
constexpr uint16_t kVxlanPort = RTE_VXLAN_DEFAULT_PORT;

// Minimum header sizes.
constexpr uint16_t kEthHdrLen = RTE_ETHER_HDR_LEN;       // 14
constexpr uint16_t kVlanHdrLen = sizeof(rte_vlan_hdr);    // 4
constexpr uint16_t kIpv4MinHdrLen = 20;
constexpr uint16_t kIpv6HdrLen = 40;
constexpr uint16_t kTcpMinHdrLen = 20;
constexpr uint16_t kUdpHdrLen = sizeof(rte_udp_hdr);      // 8
constexpr uint16_t kVxlanHdrLen = sizeof(rte_vxlan_hdr);   // 8

// ICMP type constants.
constexpr uint8_t kIcmpEchoReply = 0;
constexpr uint8_t kIcmpEchoRequest = 8;
constexpr uint8_t kIcmpv6EchoRequest = 128;
constexpr uint8_t kIcmpv6EchoReply = 129;

// Ether types in host byte order.
constexpr uint16_t kEtherTypeIpv4 = RTE_ETHER_TYPE_IPV4;  // 0x0800
constexpr uint16_t kEtherTypeIpv6 = RTE_ETHER_TYPE_IPV6;  // 0x86DD
constexpr uint16_t kEtherTypeVlan = RTE_ETHER_TYPE_VLAN;   // 0x8100
constexpr uint16_t kEtherTypeQinQ = RTE_ETHER_TYPE_QINQ;  // 0x88A8

// Returns true if `next_hdr` is an IPv6 extension header that the parser
// should walk through to find the upper-layer protocol.
static bool IsIpv6ExtensionHeader(uint8_t next_hdr) {
  return next_hdr == IPPROTO_HOPOPTS ||    // 0  — Hop-by-Hop Options
         next_hdr == IPPROTO_ROUTING ||     // 43 — Routing
         next_hdr == IPPROTO_FRAGMENT ||    // 44 — Fragment
         next_hdr == IPPROTO_ESP ||         // 50 — Encapsulating Security Payload
         next_hdr == IPPROTO_AH ||          // 51 — Authentication Header
         next_hdr == IPPROTO_DSTOPTS;       // 60 — Destination Options
}

// Parse a single layer (Ethernet + IP + L4) starting at `data + offset`.
// On success, populates meta fields and sets l2/l3/l4 lengths via out params.
// Returns kOk on success, or an error ParseResult.
static ParseResult ParseLayer(const uint8_t* data, uint16_t data_len,
                              uint16_t offset, PacketMetadata& meta,
                              uint16_t& out_l2_len, uint16_t& out_l3_len,
                              uint16_t& out_l4_len, bool& out_is_ipv6) {
  // --- Ethernet header ---
  if (offset + kEthHdrLen > data_len) {
    return ParseResult::kTooShort;
  }
  const auto* eth = reinterpret_cast<const rte_ether_hdr*>(data + offset);
  uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);
  uint16_t l2_len = kEthHdrLen;

  // Handle VLAN tags (0x8100 and 0x88A8).
  while (ether_type == kEtherTypeVlan || ether_type == kEtherTypeQinQ) {
    if (offset + l2_len + kVlanHdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* vlan =
        reinterpret_cast<const rte_vlan_hdr*>(data + offset + l2_len);
    uint16_t vlan_id = rte_be_to_cpu_16(vlan->vlan_tci) & 0x0FFF;
    if (meta.vlan_count == 0) {
      meta.outer_vlan_id = vlan_id;
      meta.vlan_count = 1;
    } else if (meta.vlan_count == 1) {
      meta.inner_vlan_id = vlan_id;
      meta.vlan_count = 2;
    }
    ether_type = rte_be_to_cpu_16(vlan->eth_proto);
    l2_len += kVlanHdrLen;
  }

  out_l2_len = l2_len;
  uint16_t ip_offset = offset + l2_len;

  // --- IP header ---
  if (ether_type == kEtherTypeIpv4) {
    out_is_ipv6 = false;

    if (ip_offset + kIpv4MinHdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* ipv4 =
        reinterpret_cast<const rte_ipv4_hdr*>(data + ip_offset);

    uint8_t ihl = ipv4->version_ihl & RTE_IPV4_HDR_IHL_MASK;
    if (ihl < 5) {
      return ParseResult::kMalformedHeader;
    }
    uint8_t version = (ipv4->version_ihl >> 4) & 0x0F;
    if (version != 4) {
      return ParseResult::kUnsupportedVersion;
    }

    uint16_t ip_hdr_len = static_cast<uint16_t>(ihl) * RTE_IPV4_IHL_MULTIPLIER;
    uint16_t total_length = rte_be_to_cpu_16(ipv4->total_length);

    if (ip_offset + ip_hdr_len > data_len) {
      return ParseResult::kTooShort;
    }
    if (total_length > data_len - ip_offset) {
      return ParseResult::kLengthMismatch;
    }

    // IPv4 options flag.
    if (ihl > 5) {
      meta.flags |= kFlagIpv4Options;
    }

    // IPv4 fragmentation detection.
    // RTE_IPV4_HDR_OFFSET_MASK = 0x1FFF, RTE_IPV4_HDR_MF_FLAG = 0x2000
    uint16_t frag_field = rte_be_to_cpu_16(ipv4->fragment_offset);
    uint16_t frag_off = frag_field & 0x1FFF;   // 13-bit fragment offset
    bool mf = (frag_field & 0x2000) != 0;      // More Fragments bit
    if (frag_off != 0 || mf) {
      meta.flags |= kFlagFragment;
      meta.frag_offset = frag_off;
    }
    if (frag_off != 0) {
      // Non-first fragment: L4 header is not present.
      meta.src_port = 0;
      meta.dst_port = 0;
      out_l3_len = ip_hdr_len;
      meta.protocol = ipv4->next_proto_id;
      meta.src_ip.v4 = ipv4->src_addr;
      meta.dst_ip.v4 = ipv4->dst_addr;
      out_l4_len = 0;
      return ParseResult::kOk;
    }

    out_l3_len = ip_hdr_len;
    meta.protocol = ipv4->next_proto_id;
    meta.src_ip.v4 = ipv4->src_addr;
    meta.dst_ip.v4 = ipv4->dst_addr;

    // --- L4 header ---
    uint16_t l4_offset = ip_offset + ip_hdr_len;
    if (meta.protocol == IPPROTO_TCP) {
      if (l4_offset + kTcpMinHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* tcp =
          reinterpret_cast<const rte_tcp_hdr*>(data + l4_offset);
      meta.src_port = rte_be_to_cpu_16(tcp->src_port);
      meta.dst_port = rte_be_to_cpu_16(tcp->dst_port);
      uint8_t data_off = (tcp->data_off >> 4) & 0x0F;
      out_l4_len = static_cast<uint16_t>(data_off) * 4;
    } else if (meta.protocol == IPPROTO_UDP) {
      if (l4_offset + kUdpHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* udp =
          reinterpret_cast<const rte_udp_hdr*>(data + l4_offset);
      meta.src_port = rte_be_to_cpu_16(udp->src_port);
      meta.dst_port = rte_be_to_cpu_16(udp->dst_port);
      out_l4_len = kUdpHdrLen;

      // UDP length validation.
      uint16_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
      uint16_t expected_udp_len = total_length - ip_hdr_len;
      if (udp_len != expected_udp_len) {
        return ParseResult::kUdpLengthMismatch;
      }
    } else if (meta.protocol == IPPROTO_ICMP || meta.protocol == IPPROTO_ICMPV6) {
      constexpr uint16_t kIcmpHdrLen = 8;
      if (l4_offset + kIcmpHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      uint8_t icmp_type = data[l4_offset];
      uint8_t icmp_code = data[l4_offset + 1];
      meta.dst_port = (static_cast<uint16_t>(icmp_type) << 8) | icmp_code;

      bool is_echo = false;
      if (meta.protocol == IPPROTO_ICMP) {
        is_echo = (icmp_type == kIcmpEchoRequest || icmp_type == kIcmpEchoReply);
      } else {
        is_echo = (icmp_type == kIcmpv6EchoRequest || icmp_type == kIcmpv6EchoReply);
      }
      if (is_echo) {
        meta.src_port = (static_cast<uint16_t>(data[l4_offset + 4]) << 8) | data[l4_offset + 5];
      } else {
        meta.src_port = 0;
      }
      out_l4_len = kIcmpHdrLen;
    } else {
      meta.src_port = 0;
      meta.dst_port = 0;
      out_l4_len = 0;
    }

  } else if (ether_type == kEtherTypeIpv6) {
    out_is_ipv6 = true;

    if (ip_offset + kIpv6HdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* ipv6 =
        reinterpret_cast<const rte_ipv6_hdr*>(data + ip_offset);

    uint8_t version = (reinterpret_cast<const uint8_t*>(ipv6)[0] >> 4) & 0x0F;
    if (version != 6) {
      return ParseResult::kUnsupportedVersion;
    }

    uint16_t payload_len = rte_be_to_cpu_16(ipv6->payload_len);
    if (static_cast<uint32_t>(kIpv6HdrLen) + payload_len >
        static_cast<uint32_t>(data_len - ip_offset)) {
      return ParseResult::kLengthMismatch;
    }

    // Copy src/dst addresses from the fixed IPv6 header before walking
    // extension headers.
    std::memcpy(meta.src_ip.v6, ipv6->src_addr.a, 16);
    std::memcpy(meta.dst_ip.v6, ipv6->dst_addr.a, 16);

    // --- IPv6 extension header chain walking ---
    uint8_t next_hdr = ipv6->proto;
    uint16_t ext_offset = ip_offset + kIpv6HdrLen;
    uint16_t total_ext_len = 0;
    bool has_ext = false;
    bool is_fragment = false;

    while (IsIpv6ExtensionHeader(next_hdr)) {
      has_ext = true;
      if (next_hdr == IPPROTO_FRAGMENT) {
        // Fragment header is always 8 bytes:
        //   next_hdr(1) + reserved(1) + frag_off_mf(2) + id(4)
        if (ext_offset + 8 > data_len) return ParseResult::kTooShort;
        uint8_t frag_next = data[ext_offset];
        uint16_t frag_field =
            (static_cast<uint16_t>(data[ext_offset + 2]) << 8) |
            data[ext_offset + 3];
        uint16_t frag_off = frag_field >> 3;
        bool mf = (frag_field & 1) != 0;
        meta.frag_offset = frag_off;
        if (frag_off != 0 || mf) {
          meta.flags |= kFlagFragment;
        }
        is_fragment = (frag_off != 0);
        next_hdr = frag_next;
        total_ext_len += 8;
        ext_offset += 8;
      } else if (next_hdr == IPPROTO_AH || next_hdr == IPPROTO_ESP) {
        // AH/ESP: stop walking — unsupported upper-layer protocol.
        meta.protocol = next_hdr;
        meta.src_port = 0;
        meta.dst_port = 0;
        out_l4_len = 0;
        if (has_ext) meta.flags |= kFlagIpv6ExtHeaders;
        out_l3_len = kIpv6HdrLen + total_ext_len;
        return ParseResult::kOk;
      } else {
        // Hop-by-Hop (0), Routing (43), Destination Options (60).
        if (ext_offset + 2 > data_len) return ParseResult::kTooShort;
        uint8_t ext_next = data[ext_offset];
        uint8_t ext_len = data[ext_offset + 1];
        uint16_t ext_size = (static_cast<uint16_t>(ext_len) + 1) * 8;
        if (ext_offset + ext_size > data_len) return ParseResult::kTooShort;
        next_hdr = ext_next;
        total_ext_len += ext_size;
        ext_offset += ext_size;
      }
    }

    if (has_ext) meta.flags |= kFlagIpv6ExtHeaders;
    out_l3_len = kIpv6HdrLen + total_ext_len;
    meta.protocol = next_hdr;

    // Non-first IPv6 fragment: L4 header is not present.
    if (is_fragment) {
      meta.src_port = 0;
      meta.dst_port = 0;
      out_l4_len = 0;
      return ParseResult::kOk;
    }

    // --- L4 header ---
    uint16_t l4_offset = ext_offset;
    if (meta.protocol == IPPROTO_TCP) {
      if (l4_offset + kTcpMinHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* tcp =
          reinterpret_cast<const rte_tcp_hdr*>(data + l4_offset);
      meta.src_port = rte_be_to_cpu_16(tcp->src_port);
      meta.dst_port = rte_be_to_cpu_16(tcp->dst_port);
      uint8_t data_off = (tcp->data_off >> 4) & 0x0F;
      out_l4_len = static_cast<uint16_t>(data_off) * 4;
    } else if (meta.protocol == IPPROTO_UDP) {
      if (l4_offset + kUdpHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* udp =
          reinterpret_cast<const rte_udp_hdr*>(data + l4_offset);
      meta.src_port = rte_be_to_cpu_16(udp->src_port);
      meta.dst_port = rte_be_to_cpu_16(udp->dst_port);
      out_l4_len = kUdpHdrLen;

      // UDP length validation.
      uint16_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
      uint16_t expected_udp_len = payload_len - total_ext_len;
      if (udp_len != expected_udp_len) {
        return ParseResult::kUdpLengthMismatch;
      }
    } else if (meta.protocol == IPPROTO_ICMP || meta.protocol == IPPROTO_ICMPV6) {
      constexpr uint16_t kIcmpHdrLen = 8;
      if (l4_offset + kIcmpHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      uint8_t icmp_type = data[l4_offset];
      uint8_t icmp_code = data[l4_offset + 1];
      meta.dst_port = (static_cast<uint16_t>(icmp_type) << 8) | icmp_code;

      bool is_echo = false;
      if (meta.protocol == IPPROTO_ICMP) {
        is_echo = (icmp_type == kIcmpEchoRequest || icmp_type == kIcmpEchoReply);
      } else {
        is_echo = (icmp_type == kIcmpv6EchoRequest || icmp_type == kIcmpv6EchoReply);
      }
      if (is_echo) {
        meta.src_port = (static_cast<uint16_t>(data[l4_offset + 4]) << 8) | data[l4_offset + 5];
      } else {
        meta.src_port = 0;
      }
      out_l4_len = kIcmpHdrLen;
    } else {
      meta.src_port = 0;
      meta.dst_port = 0;
      out_l4_len = 0;
    }

  } else {
    return ParseResult::kUnsupportedVersion;
  }

  return ParseResult::kOk;
}

// Determine layer lengths from packet_type fast path.
// Returns true if packet_type provided enough info, false to fall back.
static bool DecodePacketType(uint32_t ptype, uint16_t& l2_len,
                             uint16_t& l3_len, bool& is_ipv6,
                             bool& is_tunnel) {
  uint32_t l2_type = ptype & RTE_PTYPE_L2_MASK;
  uint32_t l3_type = ptype & RTE_PTYPE_L3_MASK;
  uint32_t tunnel_type = ptype & RTE_PTYPE_TUNNEL_MASK;

  // L2 length.
  switch (l2_type) {
    case RTE_PTYPE_L2_ETHER:
      l2_len = kEthHdrLen;
      break;
    case RTE_PTYPE_L2_ETHER_VLAN:
      l2_len = kEthHdrLen + kVlanHdrLen;
      break;
    case RTE_PTYPE_L2_ETHER_QINQ:
      l2_len = kEthHdrLen + 2 * kVlanHdrLen;
      break;
    default:
      return false;  // Unknown L2 type, fall back.
  }

  // L3 type → is_ipv6 flag and l3_len.
  if (RTE_ETH_IS_IPV4_HDR(ptype)) {
    is_ipv6 = false;
    // For IPv4, we still need to read IHL from the header for exact length.
    // Set a sentinel; caller will read the actual IHL.
    l3_len = 0;  // Will be filled by caller from header.
  } else if (RTE_ETH_IS_IPV6_HDR(ptype)) {
    is_ipv6 = true;
    l3_len = kIpv6HdrLen;
  } else if (l3_type == 0) {
    return false;  // No L3 info.
  } else {
    return false;  // Unknown L3 type.
  }

  is_tunnel = (tunnel_type == RTE_PTYPE_TUNNEL_VXLAN);
  return true;
}

}  // namespace

ParseResult PacketMetadata::Parse(Packet& pkt, PacketMetadata& meta) {
  rte_mbuf* mbuf = pkt.Mbuf();
  const uint8_t* data = pkt.Data();
  uint16_t data_len = pkt.Length();

  // 1. Checksum validation.
  if (mbuf->ol_flags &
      (RTE_MBUF_F_RX_IP_CKSUM_BAD | RTE_MBUF_F_RX_L4_CKSUM_BAD)) {
    return ParseResult::kChecksumError;
  }

  uint16_t l2_len = 0, l3_len = 0;
  bool is_ipv6 = false;
  bool is_tunnel = false;

  // 2. Try packet_type fast path.
  uint32_t ptype = mbuf->packet_type;
  bool fast_path = false;
  if (ptype != 0) {
    fast_path = DecodePacketType(ptype, l2_len, l3_len, is_ipv6, is_tunnel);
  }

  if (fast_path && !is_tunnel) {
    // Fast path for non-tunneled packets.
    // We know L2 type and IP version from packet_type, but still need to
    // parse headers for field extraction and exact lengths.
    // Fall through to manual parsing — the fast path mainly confirms
    // the protocol stack so we can skip ether_type inspection.
    // For simplicity and correctness, we use the same manual parsing
    // but skip the ether_type detection since we already know it.
  }

  // Whether fast path or slow path, we parse headers manually for field
  // extraction. The fast path value is used to detect VXLAN tunneling
  // from packet_type when available.

  if (fast_path && is_tunnel) {
    // VXLAN detected via packet_type. Parse outer headers, then inner.
    // Outer Ethernet.
    if (kEthHdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* eth = reinterpret_cast<const rte_ether_hdr*>(data);
    uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);
    uint16_t outer_l2 = kEthHdrLen;
    while (ether_type == kEtherTypeVlan || ether_type == kEtherTypeQinQ) {
      if (outer_l2 + kVlanHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* vlan =
          reinterpret_cast<const rte_vlan_hdr*>(data + outer_l2);
      ether_type = rte_be_to_cpu_16(vlan->eth_proto);
      outer_l2 += kVlanHdrLen;
    }

    // Outer IP.
    uint16_t outer_l3 = 0;
    uint16_t outer_ip_offset = outer_l2;
    if (ether_type == kEtherTypeIpv4) {
      if (outer_ip_offset + kIpv4MinHdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      const auto* ipv4 =
          reinterpret_cast<const rte_ipv4_hdr*>(data + outer_ip_offset);
      uint8_t ihl = ipv4->version_ihl & RTE_IPV4_HDR_IHL_MASK;
      if (ihl < 5) return ParseResult::kMalformedHeader;
      outer_l3 = static_cast<uint16_t>(ihl) * RTE_IPV4_IHL_MULTIPLIER;
      uint16_t total_length = rte_be_to_cpu_16(ipv4->total_length);
      if (total_length > data_len - outer_ip_offset) {
        return ParseResult::kLengthMismatch;
      }
    } else if (ether_type == kEtherTypeIpv6) {
      if (outer_ip_offset + kIpv6HdrLen > data_len) {
        return ParseResult::kTooShort;
      }
      outer_l3 = kIpv6HdrLen;
      const auto* ipv6 =
          reinterpret_cast<const rte_ipv6_hdr*>(data + outer_ip_offset);
      uint16_t payload_len = rte_be_to_cpu_16(ipv6->payload_len);
      if (static_cast<uint32_t>(kIpv6HdrLen) + payload_len >
          static_cast<uint32_t>(data_len - outer_ip_offset)) {
        return ParseResult::kLengthMismatch;
      }
    } else {
      return ParseResult::kUnsupportedVersion;
    }

    // Outer UDP + VXLAN header.
    uint16_t udp_offset = outer_l2 + outer_l3;
    if (udp_offset + kUdpHdrLen + kVxlanHdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* udp =
        reinterpret_cast<const rte_udp_hdr*>(data + udp_offset);
    if (rte_be_to_cpu_16(udp->dst_port) != kVxlanPort) {
      // packet_type said VXLAN but port doesn't match — treat as error.
      return ParseResult::kMalformedHeader;
    }

    const auto* vxlan =
        reinterpret_cast<const rte_vxlan_hdr*>(data + udp_offset + kUdpHdrLen);
    uint32_t vni = (static_cast<uint32_t>(vxlan->vni[0]) << 16) |
                   (static_cast<uint32_t>(vxlan->vni[1]) << 8) |
                   static_cast<uint32_t>(vxlan->vni[2]);

    // Set outer layer lengths on mbuf.
    mbuf->outer_l2_len = outer_l2;
    mbuf->outer_l3_len = outer_l3;

    // Parse inner headers.
    uint16_t inner_offset = udp_offset + kUdpHdrLen + kVxlanHdrLen;
    uint16_t inner_l2 = 0, inner_l3 = 0, inner_l4 = 0;
    bool inner_ipv6 = false;
    meta.frag_offset = 0;
    meta.outer_vlan_id = 0;
    meta.inner_vlan_id = 0;
    meta.vlan_count = 0;
    ParseResult result = ParseLayer(data, data_len, inner_offset, meta,
                                    inner_l2, inner_l3, inner_l4, inner_ipv6);
    if (result != ParseResult::kOk) return result;

    mbuf->l2_len = inner_l2;
    mbuf->l3_len = inner_l3;
    mbuf->l4_len = inner_l4;
    if (inner_ipv6) meta.flags |= kFlagIpv6;
    meta.vni = vni;
    return ParseResult::kOk;
  }

  // Non-tunneled path (slow path or fast path without tunnel).
  // Parse from the beginning.
  uint16_t layer_l2 = 0, layer_l3 = 0, layer_l4 = 0;
  bool layer_ipv6 = false;
  meta.frag_offset = 0;
  meta.outer_vlan_id = 0;
  meta.inner_vlan_id = 0;
  meta.vlan_count = 0;
  ParseResult result = ParseLayer(data, data_len, 0, meta,
                                  layer_l2, layer_l3, layer_l4, layer_ipv6);
  if (result != ParseResult::kOk) return result;

  // Check for VXLAN: UDP with dst port 4789.
  if (meta.protocol == IPPROTO_UDP && meta.dst_port == kVxlanPort) {
    // This is a VXLAN packet detected via manual inspection.
    uint16_t outer_l2 = layer_l2;
    uint16_t outer_l3 = layer_l3;
    uint16_t udp_offset = outer_l2 + outer_l3;

    // VXLAN header follows the UDP header.
    uint16_t vxlan_offset = udp_offset + kUdpHdrLen;
    if (vxlan_offset + kVxlanHdrLen > data_len) {
      return ParseResult::kTooShort;
    }
    const auto* vxlan =
        reinterpret_cast<const rte_vxlan_hdr*>(data + vxlan_offset);
    uint32_t vni = (static_cast<uint32_t>(vxlan->vni[0]) << 16) |
                   (static_cast<uint32_t>(vxlan->vni[1]) << 8) |
                   static_cast<uint32_t>(vxlan->vni[2]);

    mbuf->outer_l2_len = outer_l2;
    mbuf->outer_l3_len = outer_l3;

    // Parse inner headers.
    uint16_t inner_offset = vxlan_offset + kVxlanHdrLen;
    uint16_t inner_l2 = 0, inner_l3 = 0, inner_l4 = 0;
    bool inner_ipv6 = false;
    meta.frag_offset = 0;
    meta.outer_vlan_id = 0;
    meta.inner_vlan_id = 0;
    meta.vlan_count = 0;
    result = ParseLayer(data, data_len, inner_offset, meta,
                        inner_l2, inner_l3, inner_l4, inner_ipv6);
    if (result != ParseResult::kOk) return result;

    mbuf->l2_len = inner_l2;
    mbuf->l3_len = inner_l3;
    mbuf->l4_len = inner_l4;
    if (inner_ipv6) meta.flags |= kFlagIpv6;
    meta.vni = vni;
    return ParseResult::kOk;
  }

  // Non-tunneled, non-VXLAN packet.
  mbuf->l2_len = layer_l2;
  mbuf->l3_len = layer_l3;
  mbuf->l4_len = layer_l4;
  mbuf->outer_l2_len = 0;
  mbuf->outer_l3_len = 0;
  if (layer_ipv6) meta.flags |= kFlagIpv6;
  meta.vni = 0;
  return ParseResult::kOk;
}

}  // namespace rxtx
