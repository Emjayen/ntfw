#include "ntfe.h"
#include "net.h"
#include "helper.h"
#include <Windows.h>
#include <stdio.h>
#include <stdint.h>



bool EnablePrivilege(LPCWSTR lpPrivilegeName);



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
	u64 z = (x += UINT64_C(0x9E3779B97F4A7C15));
	z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
	return z ^ (z >> 31);
}

void xoro_seed(u64 seed, u64* rand_state)
{
	rand_state[0] = splitmix64(seed);
	rand_state[1] = splitmix64(seed);
}

u64 xoro_rand(u64* rand_state)
{
	const u64 s0 = rand_state[0];
	u64 s1 = rand_state[1];
	const u64 result = s0 + s1;

	s1 ^= s0;
	rand_state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	rand_state[1] = rotl(s1, 36); // c

	return result;
}


#define SRC_ADDR_COUNT  8000


#define ITERATIONS  50'000'000



static union
{
	byte data[10*8];
	u64 seed[10];
} seed_tbl =
{

	0xB5, 0x2A, 0xC7, 0x59, 0x1A, 0xEE, 0x80, 0x25, 0x82, 0x24, 0x93, 0xA1,
	0x8D, 0x18, 0xC5, 0x92, 0x60, 0x2F, 0xDC, 0x79, 0xEC, 0x9F, 0x36, 0x0A,
	0x25, 0x6D, 0xF2, 0x43, 0xEA, 0x2F, 0x55, 0xEE, 0xC9, 0x0D, 0x5B, 0xEC,
	0x74, 0xA8, 0x49, 0xEB, 0x32, 0x56, 0x72, 0xA1, 0xCA, 0xD1, 0xC1, 0xC1,
	0xD3, 0x96, 0xA0, 0x0A, 0x5E, 0xB7, 0x70, 0xFB, 0xB1, 0x9C, 0x7C, 0x16,
	0xD1, 0x94, 0x5F, 0x89, 0x53, 0x35, 0x14, 0xD7, 0xAA, 0xED, 0xD1, 0x42,
	0xFA, 0xFA, 0x5B, 0xD2, 0xDB, 0x02, 0xDB, 0xAF
};


static u32 bucket_hit[HTBL_SIZE];

u32 rand_addr[20][SRC_ADDR_COUNT];


int cmp(u32* a, u32* b)
{
	return (*b) - (*a);
}

DWORD WINAPI ThreadEntry(DWORD ThreadId)
{
	u64 rand_state[2];


	SetThreadAffinityMask(GetCurrentThread(), (3<<(ThreadId*2)));

	xoro_seed(seed_tbl.seed[ThreadId], rand_state);

	net_frame frame = {};

	frame.eth.proto = htons(ETH_P_IP);
	frame.ip.ihl = IP_IHL_MIN;
	frame.ip.saddr = 0;
	frame.ip.proto = 1;

	gs.rules[0].fsig.ether = htons(ETH_P_IP)>>7;
	gs.rules[0].fsig.proto = 1;
	gs.rules[0].in_byte_scale = 10;
	gs.rules[0].in_packet_scale = 1;

	
	u32* src_addr_list = rand_addr[ThreadId];
	
	ntfe_rx_prepare();


	for(int i = 0; i < SRC_ADDR_COUNT; i++)
	{
		src_addr_list[i] = xoro_rand(rand_state);

		auto r = host_fetch_bucket(src_addr_list[i]);

		auto idx = (r - gs.host_tbl);

		if(idx >= HTBL_SIZE)
			DebugBreak();

		bucket_hit[idx]++;
	}



	for(u64 i = 0; i < ITERATIONS; i++)
	{
		frame.ip.saddr = src_addr_list[xoro_rand(rand_state) % SRC_ADDR_COUNT];
		ntfe_rx(&frame, 64);
	}


	return 0;
}


#define THREADS  1




int main()
{


	//ntfe_init();


	SetProcessWorkingSetSize(GetCurrentProcess(), HPOOL_SIZE * 128, HPOOL_SIZE * 150);

	if(!EnablePrivilege(SE_LOCK_MEMORY_NAME))
		return -1;

	net_frame frame = {};

	frame.eth.proto = htons(ETH_P_IP);
	frame.ip.ihl = IP_IHL_MIN;
	frame.ip.saddr = 0xDEAD;
	frame.ip.proto = 1;




	HANDLE hThread[THREADS];

	for(u32 i = 0; i < THREADS; i++)
	{
		if(!(hThread[i] = CreateThread(NULL, 0x1000, (LPTHREAD_START_ROUTINE) &ThreadEntry, (LPVOID) i, CREATE_SUSPENDED, NULL)))
			return -1;
	}

	for(int i = 0; i < THREADS; i++)
		ResumeThread(hThread[i]);


	WaitForMultipleObjects(THREADS, hThread, TRUE, INFINITE);


	//qsort(bucket_hit, ARRAYSIZE(bucket_hit), sizeof(u32), (_CoreCrtNonSecureSearchSortCompareFunction) &cmp);


	//u32 zero = 0;

	//for(int i = 0; i < ARRAYSIZE(bucket_hit); i++)
	//{
	//	if(bucket_hit[i] == 0)
	//		zero++;
	//}

	//u32 dupes = 0;

	//for(int i = 0; i < THREADS * SRC_ADDR_COUNT; i++)
	//{
	//	for(int k = 0; k < THREADS * SRC_ADDR_COUNT; k++)
	//	{
	//		if(k == i)
	//			continue;

	//		if(rand_addr[k] == rand_addr[i])
	//			dupes++;
	//	}
	//}


	
	return 0;
}






/*
 * EnablePrivilege
 *
 */
bool EnablePrivilege(LPCWSTR lpPrivilegeName)
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tp;


	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
		return false;

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if(!LookupPrivilegeValueW(NULL, lpPrivilegeName, &tp.Privileges[0].Luid))
	{
		CloseHandle(hToken);
		return false;
	}

	return AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES) NULL, 0) && GetLastError() == ERROR_SUCCESS;
}
