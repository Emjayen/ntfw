/*
 * helper.h
 *
 */
#pragma once
#include <common/pce.h>





 /*
  * ipxs
  *   IPv4 to string.
  * 
  */ 
u8 ipxs(char* pd, be32 ip4);
const char* ips(be32 ip4);


/*
 * Saturated arithmetic
 *
 */
u32 satsub32(u32 a, u32 b);
u32 satadd32(u32 a, u32 b);
u32 satmul32(u32 a, u32 b);


/*
 * Bit spinlock
 *
 */
void spin_acquire(u32* lock, u8 bit);
void spin_acquire_dbg(u32* lock, u8 bit, u32* spin_count);
void spin_release(u32* lock, u8 bit);
void spin_acquire2(u32* lock);