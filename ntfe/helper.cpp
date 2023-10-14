/*
 * helper.cpp
 *
 */
#include "helper.h"



 /*
  * ipxs
  *
  */
u8 ipxs(char* pd, be32 ip4)
{
	char* ptmp = pd;
	u8 o;


	if(!ip4)
	{
		*((u64*) pd) = 0x00302E302E302E30;
		return 7;
	}

	o = (ip4&0xFF);

	for(s8 i = 0; i < 4; i++)
	{
		if((o / 100))
		{
			*pd++ = '0' + (o/100);
			o -= (o/100)*100;

			*pd++ = '0' + (o/10);
			o -= (o/10)*10;
		}

		else if((o / 10))
		{
			*pd++ = '0' + (o/10);
			o -= (o/10)*10;
		}

		*pd++ = '0' + o;
		*pd++ = '.';

		ip4 >>= 8;
		o = (ip4 & 0xFF);
	}

	*--pd = '\0';

	return (u8) (pd-ptmp);
}

u32 satsub32(u32 a, u32 b)
{
	u32 r = a - b;
	r &= -(r <= a);

	return r;
}

u32 satadd32(u32 a, u32 b)
{
	u32 r = a + b;
	r |= -(r < a);
	
	return r;
}

u32 satmul32(u32 a, u32 b)
{
	u64 r = u64(a) * u64(b);

	return u32(r) | -!!u32((r >> 32));
}



void spin_acquire(u32* lock, u8 bit)
{
	// Spin to acquire. We're guarenteed not to be preempted, as are our contender(s) and so
	// this is safe to do.
	//
	// TODO: Another possibility is MWAIT/MONITOR^, although such may be of limited utility given
	//       such brief exclusivity periods.
	//
	//       ^ In KM only until Sapphire Rapids.
	while(_interlockedbittestandset((volatile long*) lock, bit))
	{
		do
		{
			_mm_pause();
		} while(*lock & (u32(1) << bit));
	}
}

void spin_acquire_dbg(u32* lock, u8 bit, u32* spin_count)
{
	// Spin to acquire. We're guarenteed not to be preempted, as are our contender(s) and so
	// this is safe to do.
	//
	// TODO: Another possibility is MWAIT/MONITOR^, although such may be of limited utility given
	//       such brief exclusivity periods.
	//
	//       ^ In KM only until Sapphire Rapids.

	(*spin_count)--;
	do
	{
		do
		{
			(*spin_count)++;
			_mm_pause();
		} while(*lock & (u32(1) << bit));

	} while(_interlockedbittestandset((volatile long*) lock, bit));
}


void spin_release(u32* lock, u8 bit)
{
	*lock &= ~(u32(1) << bit);
}


void spin_acquire2(u32* lock)
{
	// Spin to acquire. We're guarenteed not to be preempted, as are our contender(s) and so
	// this is safe to do.
	//
	// TODO: Another possibility is MWAIT/MONITOR^, although such may be of limited utility given
	//       such brief exclusivity periods.
	//
	//       ^ In KM only until Sapphire Rapids.
	while(_InterlockedExchange((volatile long*) lock, 1))
	{
		do
		{
			_mm_pause();
		} while(*(volatile u32*) lock);
	}
}


const char* ips(be32 ip4)
{
	static char tmp[4][32];
	static u8 idx;

	idx &= 3;
	ipxs(tmp[idx], ip4);

	return tmp[idx++];
}