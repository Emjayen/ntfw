/*
 * shared.h
 *    Shared with usermode; defines the driver interface.
 * 
 */
#pragma once



// For compatibility with ANSI Win32
#ifndef _TEXT
#define _TEXT(quote) L##quote
#endif

// Maximum engine configuration file we'll accept.
#define MAX_NTFE_CFG_SZ  0x4000

// Common internal name.
#define NTFW_KMD_NAME  _TEXT("ntfwkm")

// As registered to NDIS; GUID must match INF.
#define NTFW_SERVICE_NAME   NTFW_KMD_NAME
#define NTFW_FRIENDLY_NAME  _TEXT("NT Firewall KMD")
#define NTFW_UNIQUE_NAME    _TEXT("{C6A91426-8E41-4E9C-9A1A-D0898FFE0EB9}")

// Device names for exposing to UM.
#define NTFW_DEV_NAME    _TEXT("\\Device\\") NTFW_KMD_NAME
#define NTFW_DEV_SYMLINK _TEXT("\\DosDevices\\") NTFW_KMD_NAME

// Registry key (HKLM)
#define NTFW_REG_KEY  _TEXT("SYSTEM\\CurrentControlSet\\Services\\" NTFW_SERVICE_NAME "\\Parameters")

// Registry values
#define NTFW_REG_ENGINE_CFG_FILE  _TEXT("NtfeConfigurationFile")

#define NTFW_IOCTL(Request, Method) \
	CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, Request, Method, FILE_ANY_ACCESS)



/*
 * IOCTL_NTFW_QUERY_COUNTERS
 *    Retrieve performance counters.
 *
 */
#define IOCTL_NTFW_QUERY_COUNTERS  NTFW_IOCTL(0, METHOD_BUFFERED)


 /*
  * IOCTL_NTFW_ENGINE_RESTART
  *    Restarts the firewall engine, applying any configuration changes.
  *
  */
#define IOCTL_NTFW_ENGINE_RESTART  NTFW_IOCTL(1, METHOD_BUFFERED)