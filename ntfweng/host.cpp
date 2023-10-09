/*
 * host.cpp
 *   Host state management routines.
 *
 */
#include "ntfweng.h"
#include "helper.h"
#include "eal.h"





//
// Hash function for IPv4 addresses. Distribution is known to be sufficiently
// good over sample datasets.
//
u32 inet_hashfn(be32 saddr)
{
	saddr *= 0x32583499;
	saddr ^= saddr >> 16;

	return saddr;
}


//
// Brute-force scan of bucket for address key. Returns the index
// of the entry if it exists, elsewise returns -1
//
s8 probe_bucket(htbucket* htb, be32 saddr)
{
#ifdef __AVX2__
	u32 idx = _mm256_movemask_epi8(_mm256_cmpeq_epi32(_mm256_load_si256(&htb->saddr), _mm256_set1_epi32(saddr)));
#elif __AVX__
	__m128i a = _mm_set1_epi32(saddr);
	u32 idx = _mm_movemask_epi8(_mm_cmpeq_epi32(_mm_load_si128(&((__m128i*) &htb->saddr)[0]), a));
	idx |= _mm_movemask_epi8(_mm_cmpeq_epi32(_mm_load_si128(&((__m128i*) &htb->saddr)[1]), a)) << 16;
#endif

	return (s8) (idx ? _tzcnt_u32(idx) >> 2 : -1);
}



// Grow the host pool.
void host_pool_grow()
{
	RegionCommit(gs.hpool.base + (gs.hpool.true_sz * sizeof(host_state)), HPOOL_GROW * sizeof(host_state));
	gs.hpool.true_sz += HPOOL_GROW;
}


// Allocates host state storage.
u32 host_allocate()
{
	u32 idx;
	
	
	// Trivial stack allocation of new entry. Note that 0 is reserved.
	idx = _InterlockedIncrement((volatile long*) &gs.hpool.size);

	// While the pool grows at the defined rate, growth occurs at half intervals. This
	// is done to provide a buffer-zone to reduce the likelihood of synchronization
	// if other cores come along during expansion processing.
	if(((idx - HPOOL_GROW/2) & (HPOOL_GROW-1)) == 0)
	{
		// We drew the short straw.
		host_pool_grow();
	}

	// Unlikely case where-in we have to wait for a previous core to complete the pool
	// growth.
	else if(idx >= gs.hpool.true_sz)
	{
		cpu->sc.hpl_stall++;

		do
		{
			_mm_pause();
		} while(idx >= *(volatile u32*) &gs.hpool.true_sz);
	}

	return idx;
}


//
// Resolves a host key to its bucket. This exists purely as an optimization
// to permit prefetching the bucket alone.
//
htbucket* host_fetch_bucket(be32 saddr)
{
	return &gs.host_tbl[inet_hashfn(saddr) & (HTBL_SIZE-1)];
}

s8 host_lookup(htbucket* htb, be32 saddr)
{
	// Linearly probe the bucket for the address key.
	//
	// WARN: This is a race and therefor the result may be invalid by the time we acquire
	//       it, or even inserted behind us.
	//
	// If the speculation is spurious, the side-effects will be (in order of severity):
	// 
	//     - Unauthorized traffic being treated as authorized and accepted.
	//     - An authorized host being erroneously charged for unauthorized traffic's cost.
	//     - Incorrect attribution of traffic cost among unauthorized hosts.
	//
	// Instances should be infrequent and limited to maybe a handful of packets. This is
	// considered acceptable when weighed against the substanial performance degredation 
	// that correct behaviour would require (ping-ponging the bucket line)
	//
	return probe_bucket(htb, saddr);
}



static s8 allocate(htbucket* htb, u16 tsec, u32 priority)
{
	s8 idx;


	// If the bucket isn't full, then allocate the next available entry.
	if(!htb->exhausted)
	{
		// Allocating one of the free entries we know to exist.
		if((idx = probe_bucket(htb, 0)) == 7)
			htb->exhausted = 1;
	}

	// Elsewise we need to find an eviction candidate.
	else
	{
		// Our eviction policy is simple: evict the oldest entry (FIFO) that is <= the priority class
		// of the new entry.
		u16 top = ~0;

		// TODO: SIMD maybe.
		for(s8 i = 0; i < 8; i++)
		{
			u16 dist = (~(tsec - htb->ctrl[i].age) & HTB_AGE_MASK) | (htb->ctrl[i].v & HTB_PRI_MASK);

			if(dist < top)
			{
				top = dist;
				idx = i;
			}
		}

		if(htb->ctrl[idx].pri > priority)
			return -1;
	}

	return idx;
}




s8 host_insert(htbucket* htb, be32 saddr)
{
	s8 idx;


	// As an optimization: avoid engaging in a write contest over the line (as indicated by 
	// the lock being held).
	// 
	// This behavior may infact be more broadly beneficial in the presence of spoofed attacks.
	if(HTB_TEST_LOCK(htb))
	{
		cpu->sc.htb_conflicts++;
		cpu->sc.htb_deferred++;
		return -1;
	}

	// Acquire the write-lock on the bucket.
	// 
	// TODO: A queued in-stack lock may be better, or possibly just a parallel array of bucket
	//       locks, aligned on their own cache-line.
	// 
	spin_acquire(&htb->lock, HTB_LOCK_BIT);

	// Unfortunately we now must verify we weren't usurped in our insertion attempt of this host.
	if((idx = probe_bucket(htb, saddr)) >= 0)
	{
		cpu->sc.htb_collisions++;

		// Some other core inserted our host entry. Nothing more to do than fall-through to release
		// the bucket lock.
	}

	// Inserting new key and constructing new host entry.
	else 
	{
		u16 tsec = (time_msec() >> 10) & HTB_AGE_MASK;


		// Allocate (possibly evicting existing) a new entry.
		if((idx = allocate(htb, tsec, HOST_PRI_NONE)) < 0)
		{
			// Not a major concern for unauthorized users.
		}

		else
		{
			// Initialize the new entry's key and control block.
			htb->saddr.m256i_u32[idx] = saddr;
			htb->ctrl[idx].age = tsec;
			htb->ctrl[idx].pri = HOST_PRI_NONE;
			htb->ctrl[idx].ptr = host_allocate();
		}
	}

	// Release the bucket lock.
	spin_release(&htb->lock, HTB_LOCK_BIT);

	// Return the index of the newly inserted entry.
	return idx;
}


s8 host_insert_authorized(htbucket* htb, be32 saddr, u32 priority)
{
	s8 idx;


	// Priority inserts cannot fail, and ergo we can't bail on this (just update perf. counters)
	if(HTB_TEST_LOCK(htb))
		cpu->sc.htb_conflicts++;

	// Acquire the write-lock on the bucket.
	spin_acquire(&htb->lock, HTB_LOCK_BIT);

	// Unfortunately we now must verify we weren't usurped in our insertion attempt of this host.
	if((idx = probe_bucket(htb, saddr)) >= 0)
	{
		cpu->sc.htb_collisions++;

		// Some other core inserted our host entry which may have been at a lower
		// priority; rectify it if so.
		if(htb->ctrl[idx].pri < priority)
			htb->ctrl[idx].pri = priority;

		// We're done at this point.
	}

	else
	{
		u16 tsec = (time_msec() >> 10) & HTB_AGE_MASK;

		// Allocate (possibly evicting existing) a new entry.
		if((idx = allocate(htb, tsec, HOST_PRI_NONE)) < 0)
		{
			// TODO: This may need to be considered more, albeit it's extrodinarily unlikely.
		}

		else
		{
			// Initialize the new entry's key and control block.
			htb->saddr.m256i_u32[idx] = saddr;
			htb->ctrl[idx].age = tsec;
			htb->ctrl[idx].pri = priority;
			htb->ctrl[idx].ptr = 0;
		}
	}

	// TODO:
	// Authorized hosts don't require state since it only serves to provide traffic-policing
	// of which they're exempt from. Therefor we would be able to return this host's back
	// to the pool if it were already allocated one.

	// Release the bucket lock.
	spin_release(&htb->lock, HTB_LOCK_BIT);

	// Return the index of the newly inserted entry.
	return idx;
}


host_state* get_host_state(htbucket* htb, s8 hidx)
{
	return &gs.hpool.pool[htb->ctrl[hidx].ptr];
}



