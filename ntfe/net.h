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
#define ETH_P_LOOP      0x0060  
#define ETH_P_PUP       0x0200  
#define ETH_P_PUPAT     0x0201  
#define ETH_P_IP        0x0800  
#define ETH_P_X25       0x0805  
#define ETH_P_ARP       0x0806  
#define ETH_P_BPQ       0x08FF  
#define ETH_P_DEC       0x6000  
#define ETH_P_DNA_DL    0x6001  
#define ETH_P_DNA_RC    0x6002  
#define ETH_P_DNA_RT    0x6003  
#define ETH_P_LAT       0x6004  
#define ETH_P_DIAG      0x6005  
#define ETH_P_CUST      0x6006  
#define ETH_P_SCA       0x6007  
#define ETH_P_TEB       0x6558  
#define ETH_P_RARP      0x8035  
#define ETH_P_ATALK     0x809B  
#define ETH_P_AARP      0x80F3  
#define ETH_P_8021Q     0x8100  
#define ETH_P_IPX       0x8137  
#define ETH_P_IPV6      0x86DD  
#define ETH_P_PAUSE     0x8808  
#define ETH_P_SLOW      0x8809  
#define ETH_P_WCCP      0x883E  
#define ETH_P_PPP_DISC  0x8863
#define ETH_P_PPP_SES   0x8864
#define ETH_P_MPLS_UC   0x8847
#define ETH_P_MPLS_MC   0x8848
#define ETH_P_ATMMPOA   0x884c
#define ETH_P_LINK_CTL  0x886c
#define ETH_P_ATMFATE   0x8884
#define ETH_P_PAE       0x888E    
#define ETH_P_AOE       0x88A2    
#define ETH_P_8021AD    0x88A8
#define ETH_P_802_EX1   0x88B5
#define ETH_P_TIPC      0x88CA    
#define ETH_P_8021AH    0x88E7
#define ETH_P_PTP       0x88F7    
#define ETH_P_FCOE      0x8906    
#define ETH_P_TDLS      0x890D    
#define ETH_P_FIP       0x8914    
#define ETH_P_QINQ1     0x9100    
#define ETH_P_QINQ2     0x9200    
#define ETH_P_QINQ3     0x9300    
#define ETH_P_EDSA      0xDADA    
#define ETH_P_AF_IUCV   0xFBFB


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

// Common IP protocol identifiers
#define IP_P_ICMP  0x01
#define IP_P_IGMP  0x02
#define IP_P_IPIP  0x04
#define IP_P_TCP   0x06
#define IP_P_EGP   0x08
#define IP_P_PUP   0x0C
#define IP_P_UDP   0x11
#define IP_P_DCCP  0x21
#define IP_P_IPV6  0x29
#define IP_P_RSVP  0x2E
#define IP_P_SCTP  0x84
#define IP_P_LDP   0x88


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