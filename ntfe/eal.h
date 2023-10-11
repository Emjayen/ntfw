/*
 * eal.h
 *   Provides a very thin environment abstraction layer.
 *
 */
#pragma once
#include <common\pce.h>




/*
 * QueryCurrentProcessorNumber
 *    Retrieve unique system-wide index of the current processor.
 * 
 */
u32 QueryCurrentProcessorNumber();



/*
 * HPC
 *    Fetch a high-precision clock timestamp, in units of 1/HPCHz()
 *
 */
u64 HPC();
u64 HPCHz();


/*
 * Global growable memory region. User is responsible for required synchronization.
 *
 */
void* RegionAllocate(u32 RegionSz);
bool RegionCommit(void* CommitAddress, u32 GrowSz);
bool RegionFree(void* BaseAddress, u32 CommitSz);


/*
 * Output
 *
 */
void Log(u8 Level, const char* Format, ...);

#define LOG_INFO  0
#define LOG_WARN  1
#define LOG_ERROR 2
#define LOG_FATAL 3
#define LOG_DEBUG 4

#define LINFO(fmt, ...)  Log(LOG_INFO, fmt "\n", __VA_ARGS__)
#define LWARN(fmt, ...)  Log(LOG_WARN, fmt "\n", __VA_ARGS__)
#define LERR(fmt, ...) Log(LOG_ERROR, fmt "\n", __VA_ARGS__)
#define LFATAL(fmt, ...) Log(LOG_FATAL, fmt "\n", __VA_ARGS__)
#define LDBG(fmt, ...)  Log(LOG_DEBUG, fmt "\n", __VA_ARGS__)

#define LOG LINFO