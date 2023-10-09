/*
 * net.h
 *    Protocol definitions.
 * 
 */
#pragma once
#include <common\pce.h>

#pragma pack(1)







/*
 * Ethernet II
 *
 */
#define ETH_ALEN      6 /* Address length. */
#define ETH_HLEN      14 /* Total length of header; this excludes FCS/ect. */
#define ETH_DATA_LEN  1500 /* Maximum payload length. */

// Protocol identifiers.
#define ETH_P_IP  0x0800 /* Internet v4 Protocol */
#define ETH_P_IP6 0x86DD /* Internet v6 Protocol */
#define ETH_P_ARP 0x0806 /* Address Resolution Protocol */


struct eth_addr
{
	u8 addr[ETH_ALEN];
};

struct eth_hdr
{
	eth_addr dst;
	eth_addr src;
	be16 proto;
};



/*
 * IPv4
 *
 */
#define IP_IHL_MIN  5 /* Minimum IP datagram header length (20 bytes) */

// IP flags
#define IP_CE		0x8000	/* Flag: "Congestion"		*/
#define IP_DF		0x4000	/* Flag: "Don't Fragment"	*/
#define IP_MF		0x2000	/* Flag: "More Fragments"	*/
#define IP_OFFSET	0x1FFF	/* "Fragment Offset" mask	*/

// IP protocol identifiers
#define IP_P_ICMP 0x01
#define IP_P_UDP  0x11
#define IP_P_TCP  0x06
#define IP_P_LDP  0x88

struct ip_hdr
{
	u8 ihl : 4,
	   version : 4;
	u8 tos;
	be16 len;
	be16 id;
	be16 frag_off;
	u8 ttl;
	u8 proto;
	u16 csum;
	be32 saddr;
	be32 daddr;
};



/*
 * UDP
 *
 */
struct udp_hdr
{
	be16 sport;
	be16 dport;
	be16 len;
	u16 csum;
};



/*
 * TCP
 *
 */
struct tcp_hdr
{
	be16 sport;
	be16 dport;
	be32 seq;
	be32 ack;
	u16 flags;
	be16 wndsz;
	be16 csum;
	be16 urgptr;
};


/*
 * Our assumed frame layout.
 *
 */
struct net_frame
{
	eth_hdr eth;
	ip_hdr ip;
	tcp_hdr tcp;
};


// -- 
#pragma pack()