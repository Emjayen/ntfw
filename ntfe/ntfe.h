/*
 * ntfweng.h
 *
 */
#pragma once
#include "ntfeusr.h"





// Configuration
#define HTBL_SIZE    0x40000
#define HPOOL_SIZE   (HTBL_SIZE * 8)
#define HPOOL_GROW   64 /* Host pool growth rate, in elements (host_state). */

// Limits
#define MAX_METERS NTFE_MAX_METERS
#define MAX_CPUS   NTFE_MAX_CPUS  
#define MAX_RULES  NTFE_MAX_RULES 
#define MAX_USERS  NTFE_MAX_USERS 

// Token bucket state.
struct tbucket
{
	u32 tokens; /* Tokens available. */
	u32 last; /* Last refill time. */
};

// Token bucket parameters; user defines these as their 'meter' configuration.
struct tbucket_desc
{
	u32 rate;
	u32 capacity;
	u32 level;
	u32 reserved;
};

// Flow signature; describes the network frame pattern to match for a rule.
union flowsig
{
	u32 data;

	struct
	{
		u8 ether; /* L3 protocol */
		u8 proto; /* L4 protocol */
		be16 dport; /* Destination port */
	};
};

// As taken from the compiled user program.
struct rule
{
	flowsig fsig;
	u16 in_byte_scale;
	u16 in_packet_scale;
	u16 in_syn_scale;
	u16 in_byte_tb : 3,
		in_packet_tb : 3,
		in_syn_tb : 3;
};


// Host control block; partial host state that lives in their bucket.
#define HOST_PRI_NONE     0 /* Unauthorized internet hosts. */
#define HOST_PRI_OUTAUTH  1 /* Temporarily authorized by inference from outgoing private traffic. */
#define HOST_PRI_DYNAUTH  2 /* Dynamically administratively authorized. */
#define HOST_PRI_STATIC   3 /* Statically authorized from configuration. */

#define HTB_AGE_BITS  11
#define HTB_PRI_BITS  2
#define HTB_PTR_BITS  18
#define HTB_AGE_MASK  ((1<<HTB_AGE_BITS)-1)
#define HTB_PRI_MASK  (((1<<HTB_PRI_BITS)-1) << HTB_AGE_BITS)
#define HTB_LOCK_BIT  31

#define HTB_TEST_LOCK(htb) ((htb)->lock & (u32(1)<<HTB_LOCK_BIT))


union host_ctrl
{
	// 'age' -- Timestamp when this entry was inserted, at ~second precision. Used to implement FIFO eviction. ~35 minute resolution.
	// 'pri' -- Priority; cache evictions can only be of the same priority class or lower.
	// 'ptr' -- Pointer (index into host pool) to the host state.
	// 
	// The layout within the bitfield is significant.
	struct
	{
		u32 age : HTB_AGE_BITS,
			pri : HTB_PRI_BITS,
			ptr : HTB_PTR_BITS,
			reserved : 1; /* Do not touch. */
	};

	u32 v;
};

// Host hash-table bucket
cache_align struct htbucket
{
	__m256i saddr;

	union
	{
		host_ctrl ctrl[8];

		struct
		{
			u32 lock;
			u32 __aliased : 31,
				exhausted : 1;
		};
	};
};

// Host state
cache_align struct host_state
{
	tbucket tb[MAX_METERS];
	u32 lock;
};



// Authorization configuration
cache_align struct auth_info
{
	ntfe_pkey pkey;
	u64 twnd; /* System time (UTC) window we're willing to accept keys within, in NTFT. */
};


// Simple statistics counters (per-cpu)
cache_align struct counters
{
	u32 rx_frames; /* Received frames. */
	u32 rx_bytes; /* Receive frame bytes. */
	u32 rx_malformed; /* Malformed IP packets received. */
	u32 rx_dropped; /* Count of total dropped frames. */
	u32 rx_drop_priv; /* Count of dropped frames due to private. */
	u32 rx_drop_policed; /* Count of dropped frames due to failing token bucket conformance. */
	u32 tx_frames; /* Transmitted frames. */
	u32 tx_bytes; /* Transmitted frame bytes. */
	u32 tx_malformed; /* Malformed IP packets sent. */
	u32 htb_conflicts; /* Host bucket insertion write-conflicts (lock not immediately acquired) */
	u32 htb_deferred; /* Host bucket insertion deferrals due to write contention. */
	u32 htb_collisions; /* Another core inserted the same host we were going to. */
	u32 htb_ins_fails; /* Number of insertion failures. */
	u32 hpl_stall; /* Number of times waited on host pool expansion. */
};

//
// Per-CPU instanced storage.
//
cache_align struct cpu_local
{
	// Cached high-precision timestamp.
	u64 hpc;
	u64 ts_msec;

	// Statistics
	counters sc;
	
	// Global token-buckets; all ingress costs, irrespective of host, are
	// charged against them.
	//
	// The purpose for this is purely as an optimization: by taking advantage
	// of the observation that, typically, servers will have a somewhat predictable
	// peak for their ingress rates under normal conditions, we can elide the expensive
	// per-packet-per-host processing until a user-specified hint/threshold is reached.
	//
	// They're placed on their own line [on each cpu] due to being periodically 
	// aggregated and tested (read-only) by an arbitrary core to determine if the
	// aforementioned criteria is met.
	//
	// Not currently implemented.
	//
	/* cache_align tbucket gtb; */
};


//
// All global state.
//
cache_align struct _gs
{
	struct
	{
		union
		{
			byte* base;
			host_state* pool;
		};

		u32 size; /* Current logical size of the pool, in elements. */
		u32 true_sz; /* Current committed/physically-backed size, in elements. */
	} hpool;

	u64 hpc_hz;

	u8 rule_hashsz; /* Rule table size, in bits. (Copied from config) */
	u32 rule_seed; /* Rule table hashfn seed (Copied from config) */

	rule rules[MAX_RULES];
	tbucket_desc tbdesc[MAX_METERS];
	cpu_local _cpu_local[MAX_CPUS];
	auth_info auth;
	htbucket host_tbl[HTBL_SIZE];
} extern gs;




// CPU local storage.
#define cpu (current_cpu_local())
cpu_local* current_cpu_local();

// Time services
u64 time_msec();

// Host routines
void host_pool_grow();
htbucket* host_fetch_bucket(be32 saddr);
s8 host_lookup(htbucket* htb, be32 saddr);
s8 host_insert(htbucket* htb, be32 saddr);
s8 host_insert_authorized(htbucket* htb, be32 saddr, u32 priority);
host_state* get_host_state(htbucket* htb, s8 hidx);




// Verify some things
static_assert(ispow2(HPOOL_GROW * sizeof(host_state)), "");
static_assert(ispow2(HTBL_SIZE), "");
static_assert(MAX_METERS == 4, "");
static_assert(sizeof(host_state) == CACHE_LINE_SZ, "");
static_assert(sizeof(htbucket) == CACHE_LINE_SZ, "");
