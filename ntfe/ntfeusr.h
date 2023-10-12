/*
 * ntfeusr.h
 *
 */
#pragma once
#include <common\pce.h>
#include "net.h"





// Current configuration version
#define NTFE_CFG_VERSION  1

// Internal limits
#define NTFE_MAX_METERS   4
#define NTFE_MAX_CPUS     64
#define NTFE_MAX_RULES    128
#define NTFE_MAX_USERS    32

// HMAC (SHA3) key size for use in ntfp
#define NTFE_HMAC_KEY_SZ  32 /* Bytes */

// General
#define NTFE_MAX_IDENT  32

// NTFP UDP port
#define NTFP_PORT  5662

// Public key size
#define NTFE_PKEY_SZ  32



//
// Configuration image on-disk
//
#pragma pack(1)

struct ntfe_user
{
	byte key[NTFE_HMAC_KEY_SZ];
	char ident[NTFE_MAX_IDENT];
	byte reserved[64];
};

struct ntfe_meter
{
	u32 rate;
	u32 size;
	u32 level;
	u32 reserved;
};

struct ntfe_host
{
	byte reserved[28];
	u32 saddr;
};

struct ntfe_rule
{
	u16 ether;
	u16 dport;
	u8 proto;
	u8 unused;
	u16 byte_scale;
	u16 packet_scale;
	u16 syn_scale;
	u8 byte_tb;
	u8 packet_tb;
	u8 syn_tb;
};


struct ntfe_pkey
{
	union
	{
		byte key[NTFE_PKEY_SZ];
		u64 magic; /* Low 64-bits used as protocol magic. */
	};
};

struct ntfe_config
{
	u32 reserved; /* MBZ */
	u16 version; /* 0 */
	u16 length; /* Total size of this structure, in bytes. */
	u64 create_time; /* NTFT timestamp when this configuration was authored. */
	u32 unused[8]; /* MBZ */
	ntfe_pkey pkey; /* Public key for authorized users. */
	u32 pkey_twnd; /* Time window acceptable for keys, in msec. */
	u32 rule_seed; /* Seed used to generate rule-table by user. */
	u32 rule_hashsz; /* Size of the rule table the user generated, in bits. */
	u32 rule_count; /* Number of rule table entries. */
	u32 meter_count; /* Number of token bucket descriptors. */
	u32 host_count; /* Number of statically authorized addresses configured. */
 // ntfe_rule[rule_count];
 // ntfe_meter[tbd_count]
 // ntfe_user users[user_count] 
 // ntfe_host static_hosts[static_count];
	byte ect[];
};


/*
 * ntfp -- ntfw protocol.
 *
 */
struct ntfp_hdr
{
	u64 magic; /* Magic shared key. This also generally 16-byte aligns the next field. */
	byte signature[64]; /* Ed25519 signature over the payload. */
};

struct ntfp_auth
{
	ntfp_hdr hdr;
	be32 addr; /* IPv4 address to authorize. Must match protocol header. */
};

struct ntfp_msg
{
	ntfp_hdr hdr;

	union
	{
		struct
		{
			u64 timestamp; /* NTFT UTC timestamp of message creation. */
			byte reserved[6]; /* MBZ */
			u16 len; /* Total message length, inclusive of this header. */
			
			union
			{
				ntfp_auth auth;
			};
		};

		byte payload[];
	};
};

#pragma pack()



/*
 * ntfe_init
 *    Initialize engine.
 *
 */
bool ntfe_init(ntfe_config* cfg, u32 cfg_len);


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
 * ntfe_rx_prepare
 *    Hint that a batch of transmit processing is about to commence.
 *
 */
void ntfe_tx_prepare();


/*
 * ntfe_rx
 *    Process received frame.
 *
 */
bool ntfe_rx(void* data, u16 len);


/*
 * ntfe_tx
 *   Process transmit frame.
 *
 */
void ntfe_tx(void* data, u16 len);