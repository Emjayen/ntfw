/*
 * eal.cpp
 *
 *    ntfweng's environment implementation.
 *
 */
#include "ntfweng/eal.h"
#include <ntddk.h>
#include <ntstrsafe.h>


// Our tag for growable region memory allocations
#define REGION_TAG  'rgn '



u32 QueryCurrentProcessorNumber()
{
	return KeGetCurrentProcessorNumber();
}



u64 HPC()
{
	return KeQueryPerformanceCounter(NULL).QuadPart;
}


u64 HPCHz()
{
	LARGE_INTEGER Hz;
	KeQueryPerformanceCounter(&Hz);

	return Hz.QuadPart;
}


void* RegionAllocate(u32 RegionSz)
{
	// Allocate system address-space.
	return MmAllocateMappingAddressEx(RegionSz, REGION_TAG, MM_MAPPING_ADDRESS_DIVISIBLE);
}


bool RegionCommit(void* CommitAddress, u32 GrowSz)
{
	PMDL Mdl;


	// Grab backing physical pages.
	if(!(Mdl = MmAllocatePagesForMdlEx({ 0 }, { ~0UL, ~0L }, { 0 }, GrowSz, MmCached, 0)))
		return false;

	// Populate PTEs
	MmMapLockedPagesWithReservedMapping(CommitAddress, REGION_TAG, Mdl, MmCached);

	// Not required to keep around; on teardown we'll build one for the entire region.
	ExFreePool(Mdl);

	return true;
}


bool RegionFree(void* BaseAddress, u32 CommitSz)
{
	PMDL Mdl;


	if(CommitSz)
	{
		// Construct a descriptor of the region.
		if(!(Mdl = IoAllocateMdl(BaseAddress, CommitSz, FALSE, FALSE, NULL)))
			return false;

		// Populate with the mapped PFNs.
		MmBuildMdlForNonPagedPool(Mdl);

		// Tear down translations.
		MmUnmapReservedMapping(BaseAddress, REGION_TAG, Mdl);
		ExFreePool(Mdl);
	}

	// Free the address-space.
	MmFreeMappingAddress(BaseAddress, REGION_TAG);

	return true;
}


#ifdef DBG
u8 LogLevel = 5;
#else
u8 LogLevel = 5;
#endif

void Log(u8 Level, const char* Format, ...)
{
	char str[256];

	UNREFERENCED_PARAMETER(Level);

	if(Level < LogLevel)
	{
		va_list va;
		va_start(va, Format);
		RtlStringCbVPrintfExA(str, sizeof(str), NULL, NULL, 0, Format, va);
		va_end(va);

		DbgPrint("%s", str);
	}
}