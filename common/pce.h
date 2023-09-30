/*
 * pce.h
 *
 */
#pragma once
#include <stdint.h>





// Basic types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

typedef u8 byte;

// Endian qualified types.
typedef u32 be32;
typedef u16 be16;

// Endianness
#define bswap16(x) _byteswap_ushort(x)
#define bswap32(x)  _byteswap_ulong(x)