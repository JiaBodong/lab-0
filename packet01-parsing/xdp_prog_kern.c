/* SPDX-License-Identifier: GPL-2.0 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/icmpv6.h>
#include <linux/icmp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/ip.h> // Add this line to include the definition of struct iphdr
/* Defines xdp_stats_map from packet04 */
#include "../common/xdp_stats_kern_user.h"
#include "../common/xdp_stats_kern.h"

/* Header cursor to keep track of current parsing position */
struct hdr_cursor {
	void *pos;
};

struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

static __always_inline int proto_is_vlan(__u16 h_proto)
{
        return !!(h_proto == bpf_htons(ETH_P_8021Q) ||
                  h_proto == bpf_htons(ETH_P_8021AD));
}
/* Packet parsing helpers.
 *
 * Each helper parses a packet header, including doing bounds checking, and
 * returns the type of its contents if successful, and -1 otherwise.
 *
 * For Ethernet and IP headers, the content type is the type of the payload
 * (h_proto for Ethernet, nexthdr for IPv6), for ICMP it is the ICMP type field.
 * All return values are in host byte order.
 */
static __always_inline int parse_ethhdr(struct hdr_cursor *nh,
					void *data_end,
					struct ethhdr **ethhdr)
{
	struct ethhdr *eth = nh->pos;
	int hdrsize = sizeof(*eth);

	/* Byte-count bounds check; check if current pointer + size of header
	 * is after data_end.
	 */
	if (nh->pos + hdrsize > data_end)
		return -1;

	nh->pos += hdrsize;
	*ethhdr = nh->pos;

	#pragma unroll 10
	for(int i = 0; i < 10; i++) {

		if (proto_is_vlan(eth->h_proto)) {
			struct vlan_hdr *vlh;
			vlh = nh->pos;
			if ((void *)(vlh + 1) > data_end)
				return -1;
			nh->pos += sizeof(*vlh);
			eth->h_proto = vlh->h_vlan_encapsulated_proto;
		} 
		else {
			break;
		}
	}
	

	
	
	return eth->h_proto; /* network-byte-order */
}

/* Assignment 2: Implement and use this */
static __always_inline int parse_ip6hdr(struct hdr_cursor *nh,
					void *data_end,
					struct ipv6hdr **ip6hdr)
{
	struct ipv6hdr *ip6h = nh->pos;
	int hdrsize = sizeof(*ip6h);

	if ((void *)(ip6h + 1) > data_end)
        return -1;

    *ip6hdr = ip6h;  // Set before incrementing nh->pos
    nh->pos += hdrsize;

	return ip6h->nexthdr;
}

/* Assignment 3: Implement and use this */
static __always_inline int parse_icmp6hdr(struct hdr_cursor *nh,
					  void *data_end,
					  struct icmp6hdr **icmp6hdr)
{
	struct icmp6hdr *icmp6h = nh->pos;
	int hdrsize = sizeof(*icmp6h);

	if ((void *)(icmp6h + 1) > data_end)
        return -1;

    *icmp6hdr = icmp6h;  // Set before incrementing nh->pos
    nh->pos += hdrsize;

    return icmp6h->icmp6_type;
}

static __always_inline int parse_iphdr(struct hdr_cursor *nh,
				       void *data_end,
				       struct iphdr **iphdr)
{
	struct iphdr *iph = nh->pos;
	int hdrsize = iph->ihl *4;

	if ((void *)(iph + 1) > data_end)
		return -1;

	nh->pos += hdrsize;
	*iphdr = iph;

	return iph->protocol;
}

static __always_inline int parse_icmphdr(struct hdr_cursor *nh,
					 void *data_end,
					 struct icmphdr **icmphdr)
{
	struct icmphdr *icmph = nh->pos;
	int hdrsize = sizeof(*icmph);

	if ((void *)(icmph + 1) > data_end)
		return -1;

	nh->pos += hdrsize;
	*icmphdr = icmph;

	return icmph->type;
}

SEC("xdp")
int  xdp_parser_func(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct ethhdr *eth;
	struct ipv6hdr *ip6h;
	struct icmp6hdr *icmp6h;
	struct iphdr *iph;
	struct icmphdr *icmph;
	/* Default action XDP_PASS, imply everything we couldn't parse, or that
	 * we don't want to deal with, we just pass up the stack and let the
	 * kernel deal with it.
	 */
	__u32 action = XDP_PASS; /* Default action */

        /* These keep track of the next header type and iterator pointer */
	struct hdr_cursor nh;
	int nh_type;

	/* Start next header cursor position at data start */
	nh.pos = data;


	

	/* Packet parsing in steps: Get each header one at a time, aborting if
	 * parsing fails. Each helper function does sanity checking (is the
	 * header type in the packet correct?), and bounds checking.
	 */
	nh_type = parse_ethhdr(&nh, data_end, &eth);
	if (nh_type != bpf_htons(ETH_P_IPV6))
	{
		action = XDP_ABORTED;
		goto out;
	}
	
	nh_type = parse_ip6hdr(&nh, data_end, &ip6h);
	if (nh_type != IPPROTO_ICMPV6)
	{
		action = XDP_ABORTED;
		goto out;
	}
	
	nh_type = parse_iphdr(&nh, data_end, &iph);
	if (nh_type != IPPROTO_ICMP)
	{
		action = XDP_ABORTED;
		goto out;
	}

	// /* Assignment additions go below here */
	nh_type = parse_icmp6hdr(&nh, data_end, &icmp6h);
	if (nh_type != ICMPV6_ECHO_REQUEST)
	{
		action = XDP_ABORTED;
		goto out;
	}

	nh_type = parse_icmphdr(&nh, data_end, &icmph);
	if (nh_type != ICMP_ECHO)
	{
		action = XDP_ABORTED;
		goto out;
	}
		
    if (bpf_ntohs(icmp6h->icmp6_sequence) % 2 == 0) {
        action = XDP_DROP; // drop even sequence numbers
    }

out:
	return xdp_stats_record_action(ctx, action); /* read via xdp_stats */
}

char _license[] SEC("license") = "GPL";
