/*
 * pce.h
 *
 */
#pragma once
#include <intrin.h>





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