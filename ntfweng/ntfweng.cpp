/*
 * ntwfweng.cpp
 *
 */
#include "net.h"
#include <intrin.h>




#define htons(v) bswap16(v)
#define htonl(v) bswap32(v)
#define ntohs(v) htons(v)


#define CACHE_LINE_SZ  64
#define cache_align  __declspec(align(CACHE_LINE_SZ))


// Per-CPU instanced storage.
cache_align struct cpu_local
{
	// Simple counters; global across all interfaces.
	u64 rx_frames;
	u64 rx_bytes;
	u64 tx_frames;
	u64 tx_bytes;
};

struct flowsig
{
	be16 dport; /* Destination port */
	u8 proto; /* L4 protocol */
	u8 ether; /* L3 protocol */
};



cache_align struct host_state
{
	be32 raddr; /* Remote network address. */

};



struct tbkt
{
	u64 ts_last; /* Last refill time. */
	u32 tokens; /* Tokens available. */
	u32 size; /* Bucket size, in tokens. */
};


#define AUTH_TBL_SZ  32


cache_align struct
{
	u32 seed;
	be32 host[AUTH_TBL_SZ];
} authed_hosts;



u32 hashfnA(u32 key, u32 seed)
{
	key *= seed;
	key |= key>>16;

	return key;
}


bool is_authorized_host(be32 host)
{
	return authed_hosts.host[hashfnA(host, authed_hosts.seed) & (AUTH_TBL_SZ-1)] == host;
}



u8 eth_rx(net_frame* frame, u16 len)
{
	flowsig sig;


	// All we care about here is extracting several interesting fields
	// from an assumed frame layout.
	sig.ether = (u8) frame->eth.proto;
	sig.proto = frame->ip.proto;
	sig.dport = frame->tcp.dport;

	// 
}