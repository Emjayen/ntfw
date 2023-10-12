/*
 * eal.cpp
 *
 */
#include "eal.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>




u32 QueryCurrentProcessorNumber()
{
	return GetCurrentProcessorNumber();
}


u64 HPC()
{
	u64 t;
	QueryPerformanceCounter((LARGE_INTEGER*) &t);

	return t;
}


u64 HPCHz()
{
	u64 hz;
	QueryPerformanceFrequency((LARGE_INTEGER*) &hz);

	return hz;
}


u64 QuerySystemTime()
{
	u64 t;
	GetSystemTimeAsFileTime((FILETIME*) &t);
	return t;
}


void* RegionAllocate(u32 RegionSz)
{
	return VirtualAlloc(NULL, RegionSz, MEM_RESERVE, PAGE_READWRITE);
}


bool RegionCommit(void* BaseAddress, u32 GrowSz)
{
	return VirtualAlloc(BaseAddress, GrowSz, MEM_COMMIT, PAGE_READWRITE);
}


bool RegionFree(void* BaseAddress, u32 CommitSz)
{
	return VirtualFree(BaseAddress, 0, MEM_RELEASE);
}


void Log(u8 Level, const char* Format, ...)
{
	char tmp[512];
	va_list va;
	va_start(va, Format);
	wvsprintfA(tmp, Format, va);
	va_end(va);

	printf("%s", tmp);
}