/*
 * pce.h
 *
 */
#pragma once
#include <intrin.h>
#include <memory.h>
#pragma warning(disable:4200) /* Zero-sized arrays */



// Various global assumptions
#define CACHE_LINE_SZ  64
#define SMP_PREFER_SZ  128 /* Streaming granularity on P4+ */
#define cache_align  __align(CACHE_LINE_SZ)
#define smp_align   __align(SMP_PREFER_SZ)

// Basic types
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef u8 byte;

typedef size_t uint;
typedef size_t uiptr;

// Endian qualified types.
typedef u32 be32;
typedef u16 be16;
typedef u32 le32;
typedef u16 le16;

// Endianness
#define bswap16(x) _byteswap_ushort(x)
#define bswap32(x)  _byteswap_ulong(x)

// Alignment
#define __align(x) __declspec(align(x))
#define ispow2(x) ((x & (x - 1)) == 0)

// Rounding
#define round_up(n, m) (((((size_t) (n)) + (m) - 1) / m) * m)
#define round_down(n, m) ((((size_t) (n)) / (m)) * (m))

// Minmax
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

// Prefetching
#define prefetch(addr) _mm_prefetch((const char*) addr, _MM_HINT_T0)
#define prefetcht1(addr) _mm_prefetch((const char*) addr, _MM_HINT_T1)
#define prefetcht2(addr) _mm_prefetch((const char*) addr, _MM_HINT_T2)
#define prefetchw(addr) _m_prefetchw((const volatile void*) addr)

// Bitscan
static inline u32 _bsr(u32 mask)
{
	unsigned long idx;
	_BitScanReverse(&idx, mask);
	return (u32) idx;
}

// Usual network endianness things.
#define htons(v) bswap16(v)
#define htonl(v) bswap32(v)
#define ntohs(v) htons(v)

// Memory
#define memzero(dst, len) memset(dst, 0, len)