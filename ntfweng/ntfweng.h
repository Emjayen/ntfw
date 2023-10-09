/*
 * ntfweng.h
 *
 */
#pragma once
#include <common\pce.h>





// Configuration
#define MAX_METERS   4
#define MAX_CPUS     64
#define MAX_RULES    128
#define HTBL_SIZE    0x40000
#define HPOOL_SIZE   (HTBL_SIZE * 8)
#define HPOOL_GROW   64 /* Host pool growth rate, in elements (host_state). */

// The number of host table *buckets*


// General macros
#define htons(v) bswap16(v)
#define htonl(v) bswap32(v)
#define ntohs(v) htons(v)

#define CACHE_LINE_SZ  64  /* We have no plans to be portable. */
#define SMP_PREFER_SZ  128 /* Streaming granularity on P4+ */
#define cache_align  __align(CACHE_LINE_SZ)
#define smp_align   __align(SMP_PREFER_SZ)






// Token bucket state.
struct tbucket
{
	u32 tokens; /* Tokens available. */
	u32 last; /* Last refill time. */
};

// Token bucket parameters; user defines these as their 'meter' configuration.
struct tbucket_params
{
	u32 rate;
	u32 capacity;
	u32 level;
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

// User configuration
struct ntfe_config
{
	u32 reserved; /* MBZ */
	u16 version;
	u16 length;
	u64 create_time; /* NTFT timestamp when this configuration was generated. */
	u32 unused[8]; /* MBZ */
	u32 rule_seed; /* Seed used to generate rule-table by user. */
	u8 rule_tbl_sz; /* Size of the rule table the user generated, in bits. */
	u8 rule_count; /* Number of rule table entries. */
	u16 static_count; /* Number of statically authorized addresses configured. */
	tbucket_params tbparams[MAX_METERS];
	/* rule rules[rule_count]; */
	/* be32 static_hosts[static_count]; */
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


// Simple statistics counters (per-cpu)
struct counters
{
	u32 rx_frames; /* Received frames. */
	u32 rx_bytes; /* Receive frame bytes. */
	u32 rx_malformed; /* Malformed IP packets received. */
	u32 rx_dropped; /* Count of total dropped frames. */
	u32 tx_frames; /* Transmitted frames. */
	u32 tx_bytes; /* Transmitted frame bytes. */
	u32 tx_malformed; /* Malformed IP packets sent. */
	u32 htb_conflicts; /* Host bucket insertion write-conflicts (lock not immediately acquired) */
	u32 htb_deferred; /* Host bucket insertion deferrals due to write contention. */
	u32 htb_collisions; /* Another core inserted the same host we were going to. */
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
	cache_align tbucket gtb;
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


	u8 rule_tbl_sz; /* Rule table size, generated by the user, in bits. */
	u32 rule_tbl_seed; /* Generated by the user to produce their rule table. */
	tbucket_params tbcfg[MAX_METERS];

	rule rules[MAX_RULES];
	cpu_local _cpu_local[MAX_CPUS];
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


/*
 * ntfe_init
 *    Initialize engine.
 *
 */
bool ntfe_init();


/*
 * ntfe_cleanup
 *    Cleanup engine.
 *
 */
void ntfe_cleanup();


/*
 * ntfe_rx_prepare
 *    Hint that a batch of receive processing is about to commence.
 *
 */
void ntfe_rx_prepare();



/*
 * ntfe_rx
 *    Process received frame.
 *
 */
bool ntfe_rx(void* data, u16 len);


// Verify some things
static_assert(ispow2(HPOOL_GROW * sizeof(host_state)), "");
static_assert(ispow2(HTBL_SIZE), "");
static_assert(MAX_METERS == 4, "");
static_assert(sizeof(host_state) == CACHE_LINE_SZ, "");
static_assert(sizeof(htbucket) == CACHE_LINE_SZ, "");
