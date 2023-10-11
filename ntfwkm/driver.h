/*
 * driver.h
 *
 */
#pragma once



// Common internal name.
#define NTFW_KMD_NAME  L"ntfwkm"

// As registered to NDIS; GUID must match INF.
#define FLT_SERVICE_NAME   NTFW_KMD_NAME
#define FLT_FRIENDLY_NAME  L"NT Firewall KMD"
#define FLT_UNIQUE_NAME    L"{C6A91426-8E41-4E9C-9A1A-D0898FFE0EB9}"

// Device names for exposing to UM.
#define NTDEV_NAME    L"\\Device\\" NTFW_KMD_NAME
#define NTDEV_SYMLINK L"\\DosDevices\\" NTFW_KMD_NAME


#define NTFW_IOCTL(Request, Method) \
	CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, Request, Method, FILE_ANY_ACCESS)



/*
 * IOCTL_NTFW_QUERY_COUNTERS
 *    Retrieve performance counters.
 *
 */
#define IOCTL_NTFW_QUERY_COUNTERS  NTFW_IOCTL(0, METHOD_BUFFERED);