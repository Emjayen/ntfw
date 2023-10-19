/*
 * ntfeusr.cpp
 *
 */
#include "ntfeusr.h"




u16 rule_hashfn(ntfe_flowsig fs, u32 seed)
{
	u32 a = _rotl((seed + 0x165667B5) - (fs.data * 0x3D4D51C3), 17) * 0x27D4EB2F;
	u32 b = (a ^ (a >> 15)) * 0x85EBCA77;
	u32 c = (b ^ (b >> 13)) * 0x0C2B2AE3D;

	return (u16) (c ^ (c >> 16));
}


u16 ntfe_rule_hashfn(u32 seed, le16 ethertype, le16 port, u8 proto)
{
	ntfe_flowsig fs;

	fs.ether = ETHER_LE16_TO_FS(ethertype);
	fs.proto = proto;
	fs.dport = htons(port);

	return rule_hashfn(fs, seed);
}
