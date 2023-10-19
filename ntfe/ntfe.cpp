/*
 * ntwfweng.cpp
 *
 */
#include "net.h"
#include "eal.h"
#include "ntfe.h"
#include "helper.h"
#include "ed25519/ed25519.h"
#include <intrin.h>
#include <memory.h>




// Global state.
_gs gs;




cpu_local* current_cpu_local()
{
	// Note that this call will compile down into a PCRB reference
	// which is implemented as an inlined segment offset address 
	// calculation and is therefor only slightly worse than a direct
	// address calculation.
	return &gs.cpuls[QueryCurrentProcessorNumber()];
}


u64 time_msec()
{
	return cpu->ts_msec;
}


u16 hash_flowsig(flowsig fs)
{
	return rule_hashfn(fs, gs.rule_seed) & ((1<<gs.rule_hashsz)-1);
}


rule* rule_lookup(flowsig fs)
{
	rule& rl = gs.rules[hash_flowsig(fs)];

	return rl.fsig.data == fs.data ? &rl : NULL;
}


bool rule_insert(ntfe_rule* rl, u32 seed, u8 hashsz, bool test)
{
	rule& rdst = gs.rules[ntfe_rule_hashfn(seed, rl->ether, rl->dport, rl->proto) & ((u16(1) << hashsz) - 1)];

	if(rdst.fsig.data != 0)
		return false;

	if(test)
		return true;

	rdst.fsig.ether = ETHER_LE16_TO_FS(rl->ether);
	rdst.fsig.proto = rl->proto;
	rdst.fsig.dport = htons(rl->dport);
	
	rdst.in_byte_scale = rl->byte_scale;
	rdst.in_packet_scale = rl->packet_scale;
	rdst.in_syn_scale = rl->syn_scale;
	rdst.in_byte_tb = rl->byte_tb;
	rdst.in_packet_tb = rl->packet_tb;
	rdst.in_syn_tb = rl->syn_tb;

	return true;
}


bool token_bucket_charge(tbucket* tb, tbucket_desc* tbd, u32 tmsec, u32 cost)
{
	// Replenish bucket with tokens over delta.
	const u32 dt = tmsec - tb->last;

	if(dt)
	{
		tb->tokens = min(tbd->capacity, satadd32(tb->tokens, satmul32(dt, tbd->rate)));
		tb->last = tmsec;
	}

	// Remove from the bucket.
	tb->tokens = satsub32(tb->tokens, cost);

	// Return if we passed.
	return tb->tokens > tbd->level;
}


bool host_charge_packet(host_state* host, rule* rl, u16 in_bytes)
{
	u8 result = 2;


	// Compute costs.
	const u32 in_bytes_cost = satmul32(in_bytes, rl->in_byte_scale);
	const u32 in_packet_cost = rl->in_packet_scale;

	// While atomics could be used, given the low contention, the serializing overhead of their
	// usage favors a simple spinlock instead.
	spin_acquire(&host->lock, 0);

	// Charge each of the user's per-operation buckets.
	const u32 tmsec = (u32) time_msec();

	result -= in_bytes_cost ? token_bucket_charge(&host->tb[rl->in_byte_tb], &gs.tbdesc[rl->in_byte_tb], tmsec, in_bytes_cost) : 1;
	result -= in_packet_cost ? token_bucket_charge(&host->tb[rl->in_packet_tb], &gs.tbdesc[rl->in_packet_tb], tmsec, in_packet_cost) : 1;

	// Return non-zero if we failed to conform to any token bucket.
	spin_release(&host->lock, 0);

	return result;
}



static void ntfp_rx_process(net_frame* frame, u16 len)
{
	ntfp_msg* msg = (ntfp_msg*) &(&frame->udp)[1];
	

	// Verify magic; elides the need for expensive processing ahead. This
	// access is legal as we're guarenteed atleast 64 bytes of frame
	// data. 
	if(msg->hdr.magic != gs.pkey.magic)
		return;

	// Start bringing in the key.
	prefetcht1(&gs.pkey);

	// Pull link header & validate network.
	if((len -= sizeof(eth_hdr)) < sizeof(ip_hdr) || len < ntohs(frame->ip.len))
		return;

	// May truncate due to link padding.
	len = ntohs(frame->ip.len);

	// Pull network header & validate transport.
	if((len -= sizeof(ip_hdr)) < sizeof(udp_hdr) || len < ntohs(frame->udp.len))
		return;

	// Pull transport header & validate application.
	if((len -= sizeof(udp_hdr)) < sizeof(ntfp_hdr) || len < msg->len)
		return;
	
	// Verify it's within the left and right edges of the acceptability window.
	const auto t = QuerySystemTime();

	if(msg->timestamp < t - (AUTH_TWND/2) || msg->timestamp > t + (AUTH_TWND/2))
		return;

	// Verify the signature of the payload matches with the configured public key.
	if(!ed25519_verify(msg->hdr.signature, msg->payload, msg->len, gs.pkey.key))
		return;

	// This would be very wrong.
	if(msg->auth.addr == 0)
		return;

	// Passed all checks; register as an authorized host.
	if(host_insert_authorized(host_fetch_bucket(msg->auth.addr), msg->auth.addr, HOST_PRI_DYNAUTH) < 0)
	{
		// TODO: Should log this.
	}
}


bool ntfe_rx(void* data, u16 len)
{
	net_frame* frame = (net_frame*) data;
	htbucket* htb;
	rule* rl;
	flowsig sig;
	u32 saddr;
	u32 mask;
	s8 hidx;
	
	

	// Extract the fields that constitute the flow signature.
	sig.ether = ETHER_BE16_TO_FS(frame->eth.proto);
	sig.proto = frame->ip.proto;
	sig.dport = frame->tcp.dport;

	LDBG("rx: sig.ether=%u sig.proto=%u sig.dport=%u (frame->ether:0x%X frame->proto:%u)", sig.ether, sig.proto, sig.dport, bswap16(frame->eth.proto), frame->ip.proto);

	// For non-IPv4 traffic the rules are simply inclusive for accept.
	if(frame->eth.proto != htons(ETH_P_IP))
	{
		sig.data &= 0xFF;

		if(!rule_lookup(sig))
			goto drop;

		else
			goto accept;
	}

	//
	// IPv4 processing.
	// 
	// Some hardcoded filtering of:
	//
	//    Protocol options - these are effectively deprecated outside some
	//                       esoteric environments & their only notable use
	//                       in recent time being host stack exploits.
	//
	//    Fragmentation - obsolete; if they manage to survive the gauntlet 
	//                    of routers it's likely for the purpose of abusing
	//                    the host stack's reassembly handling.
	//
	// N.B: We're assuming a fixed-offset L4 header, therefor protocol options
	//      would require restructuring regardless.
	//
	mask = 0x0000FFFF;

	if(frame->ip.ihl != IP_IHL_MIN)
	{
		cpu->sc.rx_malformed++;
		goto drop;
	}

	// This should never be possible, however it may occur due to local software, and
	// such would cause us to explode so we are forced to check.
	if((saddr = frame->ip.saddr) == 0)
	{
		cpu->sc.rx_malformed++;
		goto drop;
	}

	// This is the earliest point in processing we can safely know the source address; 
	// start bringing in the containing bucket. Unfortunately still unlikely to hide 
	// much of the latency.
	htb = host_fetch_bucket(saddr);
	prefetch(htb);

	if(frame->ip.frag_off & htons(IP_MF | IP_OFFSET))
	{
		cpu->sc.rx_malformed++;
		goto drop;
	}

	if(sig.proto == IP_P_UDP || sig.proto == IP_P_TCP || sig.proto == IP_P_LDP)
	{
		// While technically not a violation of any RFC, port zero is defacto invalid.
		// 
		// If accepted it would also break our signature identification due to aliasing,
		// so we're dropping them on the floor.
		if(sig.dport == 0)
		{
			cpu->sc.rx_malformed++;
			goto drop;
		}

		// Port field is valid.
		mask |= 0xFFFF0000;
	}

	// If the host is authorized then everything is always accepted.
	if((hidx = host_lookup(htb, saddr)) >= 0 && htb->ctrl[hidx].pri != HOST_PRI_NONE)
		goto accept;

	// NTFP hook.
	if(sig.data == ((u32(IP_P_UDP)<<8) | u32((htons(NTFP_PORT))<<16)))
	{
		ntfp_rx_process(frame, len);
		goto drop;
	}

	// If it exists, start bringing in the host-state in preperation for modifying the
	// the token buckets.
	if(hidx >= 0 && htb->ctrl[hidx].ptr)
		prefetchw(get_host_state(htb, hidx));

	// See if there's a rule for this packet. If no rule exists, then by definition
	// this is private traffic, and we've already established this is not an authorized
	// host so in which case: drop.
	sig.data &= mask;

	if(!(rl = rule_lookup(sig)))
	{
		cpu->sc.rx_drop_priv++;
		goto drop;
	}

	// At this point:
	//   - This is public traffic.
	//   - Host is known to be un-authorized.
	//   - It may or may not have a host record in the table.
	
	// If the user hasn't configured traffic policing then the traffic is unconditionally accepted.
	if(!gs.hpool)
		goto accept;

	// If traffic policing is enabled however, we need to first create a new host entry
	// in the case it doesn't already exist.
	if(hidx < 0 && (hidx = host_insert(htb, saddr)) < 0)
	{
		// Insertion failure is possible, however it should only ever be transient. In this
		// case we drop the packet.
		goto drop;
	}

	// Apply charges, checking conformance.
	if(host_charge_packet(get_host_state(htb, hidx), rl, len - sizeof(eth_hdr)))
	{
		cpu->sc.rx_drop_policed++;
		goto drop;
	}

accept:
	return true;

drop:
	cpu->sc.rx_dropped++;
	return false;
}



void ntfe_tx(void* data, u16 len)
{
	net_frame* frame = (net_frame*) data;
	htbucket* htb;
	flowsig sig;
	u32 mask;
	s8 hidx;

	LDBG("ntfe_tx(): %s", ips(frame->ip.daddr));
	// Extract the fields that constitute the flow signature.
	sig.ether = (u8) (frame->eth.proto >> 7);
	sig.proto = frame->ip.proto;
	sig.dport = frame->tcp.dport;

	// Not interested in non-IP
	if(frame->eth.proto != htons(ETH_P_IP))
		return;

	// Unlike the rx path, we only need to do minimal validation here.
	if(frame->ip.ihl != IP_IHL_MIN)
	{
		cpu->sc.tx_malformed++;
		return;
	}

	mask = 0x0000FFFF;

	// Extract the destination address and fetch their containing bucket.
	auto daddr = frame->ip.daddr;
	htb = host_fetch_bucket(daddr);

	if(sig.proto == IP_P_UDP || sig.proto == IP_P_TCP || sig.proto == IP_P_LDP)
	{
		// Port field is valid.
		mask |= 0xFFFF0000;
	}

	// Only interested in private traffic.
	sig.data &= mask;

	if(rule_lookup(sig))
		return;

	// Okay it's private traffic; mark the destination as now being an authorized
	// source.
	//
	// N.B: While there may exist a record for this host, it's plausible this machine
	//      made contact with it earlier from a public range.
	//
	if((hidx = host_lookup(htb, daddr)) < 0 || htb->ctrl[hidx].pri < HOST_PRI_OUTAUTH)
		host_insert_authorized(htb, daddr, HOST_PRI_OUTAUTH);

	char tmp[32];
	ipxs(tmp, daddr);
	LDBG("Authorized dynamic outgoing: %s (hidx:%d)", tmp, hidx);
}


void ntfe_rx_prepare()
{
	// Start bringing in memory we're reliably going to touch.
	prefetcht1(&gs);
	prefetcht1(cpu);
	prefetchw(&cpu->sc);
	prefetcht1(&gs.rules);
	
	// Refresh the high-precision clock timestamp cache of the current 
	// processor with a new sample.
	cpu->ts_hpc = HPC();
	cpu->ts_msec = (cpu->ts_hpc * 1000) / gs.hpc_hz;
}


void ntfe_tx_prepare()
{
	ntfe_rx_prepare();
}


bool ntfe_validate_configuration(ntfe_config* cfg, u32 cfg_len)
{
	union
	{
		byte* data;
		ntfe_rule* rl;
		ntfe_meter* meter;
		ntfe_host* host;
	}; data = cfg->ect;


	// Basic bounds check.
	if(cfg_len < sizeof(ntfe_config) || cfg->length > cfg_len)
	{
		LERR("[cfg] Insufficient load data (%u)", cfg_len);
		return false;
	}

	if(cfg->length < sizeof(ntfe_config) +
		(cfg->rule_count * sizeof(ntfe_rule)) +
		(cfg->meter_count * sizeof(ntfe_meter)) +
		(cfg->host_count * sizeof(ntfe_host)))
	{
		LERR("[cfg] Incorrect data length (%u)", cfg->length);
		return false;
	}

	// Check reserved fields.
	if(cfg->reserved)
	{
		LERR("[cfg] Corrupt reserved field.");
		return false;
	}

	// Version check
	if(cfg->version != NTFE_CFG_VERSION)
	{
		LERR("[cfg] Version mismatch (%u != %u)", cfg->version, NTFE_CFG_VERSION);
		return false;
	}

	// Check rules
	if(cfg->rule_count > MAX_RULES || (1<<cfg->rule_hashsz) > MAX_RULES || cfg->rule_hashsz == 0)
	{
		LERR("[cfg] Bad rules section; rule_count:%u rule_hashsz:%u (max:%u)", cfg->rule_count, cfg->rule_hashsz, MAX_RULES);
		return false;
	}

	// Verify there's no rule table collisions.
	for(uint i = 0; i < cfg->rule_count; i++, rl++)
	{
		if(!rule_insert(rl, cfg->rule_seed, cfg->rule_hashsz, true))
		{
			LERR("[cfg] Rule hashtable collision when inserting [0x%X, 0x%X, %u] with seed 0x%X", rl->ether, rl->proto, rl->dport, cfg->rule_seed);
			return false;
		}
	}

	// Check meters
	if(cfg->meter_count > MAX_METERS)
	{
		LERR("[cfg] Unsupported meter count: %u (max:%u)", cfg->meter_count, MAX_METERS);
		return false;
	}

	return true;
}


bool ntfe_configure(ntfe_config* cfg, u32 cfg_len)
{
	union
	{
		byte* data;
		ntfe_meter* meter;
		ntfe_rule* rl;
		ntfe_host* host;
	}; data = cfg->ect;


	// This -should- have already been done, but we're doing it again to be sure.
	if(!ntfe_validate_configuration(cfg, cfg_len))
	{
		LERR("Attempted to apply malformed configuration.");
		return false;
	}

	// Remember to clean any zero-initialized assumptions as this may be a restart.
	memzero(gs.rules, sizeof(gs.rules));
	memzero(gs.tbdesc, sizeof(gs.tbdesc));
	memzero(&gs.pkey, sizeof(gs.pkey));

	// Invalidate all hosts currently marked as statically authorized.
	host_clear_statically_authorized();

	// Load rules.
	gs.rule_hashsz = cfg->rule_hashsz;
	gs.rule_seed = cfg->rule_seed;

	for(uint i = 0; i < cfg->rule_count; i++, rl++)
	{
		if(!rule_insert(rl, cfg->rule_seed, cfg->rule_hashsz, false))
			return false;
	}

	// Load meters
	for(uint i = 0; i < cfg->meter_count; i++)
	{
		ntfe_meter& src = *meter++;
		tbucket_desc& dst = gs.tbdesc[i];

		dst.rate = src.rate;
		dst.capacity = src.size;
		dst.level = src.level;
	}

	// Load statically authorized hosts.
	for(uint i = 0; i < cfg->host_count; i++)
	{
		ntfe_host& src = *host++;

		if(host_insert_authorized(host_fetch_bucket(src.saddr), src.saddr, HOST_PRI_STATIC) < 0)
		{
			LWARN("[cfg] Failed to insert statically-authorized host (0x%X)", src.saddr);
			return false;
		}
	}

	return true;
}


bool ntfe_init()
{
	// Cache the frequency of the high-precision clock source. This is established at
	// boot-time and is guarenteed to be stable.
	gs.hpc_hz = HPCHz();

	// Allocate the host-state pool.
	if(!(gs.hpool = (host_state*) RegionAllocate(HPOOL_SIZE * sizeof(host_state))))
		return false;

	// Grow by an initial growth amount.
	host_pool_grow();

	return true;
}


void ntfe_cleanup()
{
	// Release the host-state pool.
	if(gs.hpool)
		RegionFree(gs.hpool, gs.hp_true_sz  * sizeof(host_state));

	gs.hpool = NULL;
}

