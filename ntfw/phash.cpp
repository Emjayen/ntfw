/*
 * phash.cpp
 *   Find perfect hash [seed] for rule table.
 *
 */
#include "ntfw.h"







/*
 * xoroshiro128+ implementation
 *
 */
static inline u64 rotl(const u64 x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static u64 splitmix64(u64& x)
{
	u64 z = (x += u64(0x9E3779B97F4A7C15));
	z = (z ^ (z >> 30)) * u64(0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * u64(0x94D049BB133111EB);
	return z ^ (z >> 31);
}

void xoro_seed(u64 seed, u64 (&state)[2])
{
	state[0] = splitmix64(seed);
	state[1] = splitmix64(seed);
}

u64 xoro_rand(u64 (&state)[2])
{
	const u64 s0 = state[0];
	u64 s1 = state[1];
	const u64 result = s0 + s1;

	s1 ^= s0;
	state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	state[1] = rotl(s1, 36); // c

	return result;
}



static u32 GetProcessorCount()
{
	union
	{
		SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* pi;
		byte* ptr;
	};

	DWORD Result;
	u32 Count = 1;

	if(!GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &(Result = 0)) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		goto done;

	pi = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) alloca(Result);

	if(!GetLogicalProcessorInformationEx(RelationProcessorCore, pi, &Result))
		goto done;

	while(Result && Result > pi->Size)
	{
		Result -= pi->Size;
		ptr += pi->Size;
		Count++;
	}

done:
	return Count;
}




cache_align static volatile u32 record_bits;
cache_align static volatile u32 record_seed;


struct
{
	volatile bool quit_signal;
	u32 target_bits;
	u32 rule_num;
	ntfe_rule* rules;
} cache_align static cfg;


static DWORD CALLBACK ThreadEntry(VOID*)
{
	u64 xoro_state[2];
	u8 table[NTFE_MAX_RULES];

	xoro_seed(GetCurrentThreadId(), xoro_state);

next:
	while(!cfg.quit_signal && record_bits > cfg.target_bits)
	{
		const u32 seed = xoro_rand(xoro_state);
		const u32 bits = record_bits - 1;
		const u32 mask = (1<<bits)-1;

		memzero(table, sizeof(table));
		

		for(u32 i = 0; i < cfg.rule_num; i++)
		{
			u16 h = (ntfe_rule_hashfn(seed, cfg.rules[i].ether, cfg.rules[i].dport, cfg.rules[i].proto) & mask);

			if(table[h])
				goto next;

			table[h] = 1;
		}

		if(_InterlockedCompareExchange(&record_bits, bits, bits+1) == bits+1)
			record_seed = seed;
	}

	return TRUE;
}


bool FindRuleMagic(ntfe_rule* pRules, u32 RuleNum, u32* pSeed, u32* pHashSz, u32 MaximumTimeMsec)
{
	HANDLE* phThreads;


	cfg.target_bits = _bsr(RuleNum) + 1;
	cfg.rules = pRules;
	cfg.rule_num = RuleNum;
	cfg.quit_signal = false;

	record_bits = _bsr(NTFE_MAX_RULES) + 1;
	record_seed = 0;


	// Spin up some threads to find the seed.
	auto ThreadCount = GetProcessorCount();

	phThreads = (HANDLE*) _alloca(sizeof(HANDLE) * ThreadCount);

	for(uint i = 0; i < ThreadCount; i++)
	{
		if(!(phThreads[i] = CreateThread(NULL, 0x1000, &ThreadEntry, NULL, 0, NULL)))
			return false;
	}

	WaitForMultipleObjects(ThreadCount, phThreads, FALSE, MaximumTimeMsec);


	cfg.quit_signal = true;
	WaitForMultipleObjects(ThreadCount, phThreads, TRUE, INFINITE);

	for(uint i = 0; i < ThreadCount; i++)
		CloseHandle(phThreads[i]);

	if(record_bits > _bsr(NTFE_MAX_RULES))
		return false;

	*pSeed = record_seed;
	*pHashSz = record_bits;

	return true;
}