/*
 * driver.cpp
 *
 */
#include <ndis.h>
#include "driver.h"
#include "config.h"

#pragma warning(disable:4706) // Assignment within conditional

UINT8 ipxs(char* pd, UINT sz, UINT ip4);
// kdnet key: OTEZ1DWUHDTC.KYXZ014NPQLL.EU55ND8LPQ21.77Y47FBYRSW4


// Filter states
enum FILTER_STATE
{
	FilterStateUndefined,
	FilterStateAttached,
	FilterStatePausing,
	FilterStatePaused,
	FilterStateRunning,
	FilterStateRestarting,
	FilterStateDetaching,
	FilterStateDetached,
};

// Supported media of the lower-edge device. This must match
// the binding as specified in the INF.
const NDIS_MEDIUM SupportedNdisMedia[] =
{
	NdisMedium802_3,
	NdisMediumWan, /* Is really just 802.3 */
};

// Filter instance (module) context.
struct FLT_MODULE
{
	SLIST_ENTRY Next;
	NDIS_HANDLE FilterModuleHandle;
	FILTER_STATE State;
};

// All of our filter modules & freelist.
FLT_MODULE FltModules[MAX_FILTER_MODULES];
SLIST_HEADER FltModuleList;
SLIST_HEADER FltModuleFreeList;

// Globals
NDIS_HANDLE FltDriverHandle;
PDEVICE_OBJECT FltDeviceObject;
NDIS_HANDLE FltDeviceHandle;

// A big lock; serializes access for control operations (very infrequent)
KSPIN_LOCK DriverLock;





VOID SetModuleState(FLT_MODULE* Module, FILTER_STATE NewState)
{
	static const char* STATE[] =
	{
		"FilterStateUndefined",
		"FilterStateAttached",
		"FilterStatePausing",
		"FilterStatePaused",
		"FilterStateRunning",
		"FilterStateRestarting",
		"FilterStateDetaching",
		"FilterStateDetached"
	};

	LOG(LINFO, "[%zu] %s -> %s", (Module - FltModules), STATE[Module->State], STATE[NewState]);

	Module->State = NewState;
}


NDIS_STATUS FilterAttach
(
	NDIS_HANDLE NdisFilterHandle,
	NDIS_HANDLE FilterDriverContext,
	PNDIS_FILTER_ATTACH_PARAMETERS AttachParameters
)
{
	NTSTATUS Status = NDIS_STATUS_SUCCESS;
	NDIS_FILTER_ATTRIBUTES  FilterAttr;
	FLT_MODULE* FilterModule;


	UNREFERENCED_PARAMETER(NdisFilterHandle);
	UNREFERENCED_PARAMETER(FilterDriverContext);

	// Verify the media type is supported. This should probably be a fatal
	// condition if not, however we'll be tolerant and just indicate we can't.
	for(INT i = 0;;)
	{
		if(AttachParameters->MiniportMediaType == SupportedNdisMedia[i])
			break;

		else if(++i == ARRAYSIZE(SupportedNdisMedia))
		{
			LOG(LWARN, "Unable to attach; unsupported media.");
			Status = NDIS_STATUS_FAILURE;
			goto done;
		} 
	}

	LOG(LINFO, "Attaching to %S; BaseMiniportIfIndex:%u IfIndex:%u LowerIfIndex:%u", AttachParameters->BaseMiniportName->Buffer, AttachParameters->BaseMiniportIfIndex, AttachParameters->IfIndex, AttachParameters->LowerIfIndex);

	// Allocate filter context state/module.
	if(!(FilterModule = (FLT_MODULE*) ExInterlockedPopEntrySList(&FltModuleFreeList, &DriverLock)))
	{
		LOG(LERR, "Unable to attach; filter allocation failed.");
		Status = NDIS_STATUS_FAILURE;
		goto done;
	}

	FilterModule = CONTAINING_RECORD(FilterModule, FLT_MODULE, Next);
	FilterModule->FilterModuleHandle = NdisFilterHandle;
	
	SetModuleState(FilterModule, FilterStateAttached);

	NdisZeroMemory(&FilterAttr, sizeof(FilterAttr));
	FilterAttr.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
	FilterAttr.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
	FilterAttr.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
	FilterAttr.Flags = 0;

	NDIS_DECLARE_FILTER_MODULE_CONTEXT(FLT_MODULE);

	if((Status = NdisFSetAttributes(NdisFilterHandle, (NDIS_HANDLE) FilterModule, &FilterAttr)) != NDIS_STATUS_SUCCESS)
	{
		LOG(LWARN, "Failed to set filter attributes.");
		goto done;
	}

done:
	return Status;
}


VOID FilterDetach
(
	NDIS_HANDLE FilterModuleContext
)
{
	FLT_MODULE* Context = (FLT_MODULE*) FilterModuleContext;

	SetModuleState(Context, FilterStateDetached);

	ExInterlockedPushEntrySList(&FltModuleFreeList, &Context->Next, &DriverLock);
}


NDIS_STATUS FilterPause
(
	NDIS_HANDLE FilterModuleContext,
	PNDIS_FILTER_PAUSE_PARAMETERS PauseParameters
)
{
	UNREFERENCED_PARAMETER(PauseParameters);

	FLT_MODULE* Context = (FLT_MODULE*) FilterModuleContext;

	SetModuleState(Context, FilterStatePaused);

	return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS FilterRestart
(
	NDIS_HANDLE FilterModuleContext,
	PNDIS_FILTER_RESTART_PARAMETERS RestartParameters
)
{
	UNREFERENCED_PARAMETER(RestartParameters);

	FLT_MODULE* Context = (FLT_MODULE*) FilterModuleContext;
	
	SetModuleState(Context, FilterStateRestarting);
	SetModuleState(Context, FilterStateRunning);

	return NDIS_STATUS_SUCCESS;
}


VOID FilterReceive
(
	NDIS_HANDLE FilterModuleContext,
	PNET_BUFFER_LIST NetBufferLists,
	NDIS_PORT_NUMBER PortNumber,
	ULONG NumberOfNetBufferLists,
	ULONG ReceiveFlags
)
{
	FLT_MODULE* Context = (FLT_MODULE*) FilterModuleContext;

	UCHAR Frame[64];
	UCHAR* Header;

	for(NET_BUFFER_LIST* nbl = NetBufferLists; nbl; nbl = nbl->Next)
	{
		auto FrameLength = nbl->FirstNetBuffer->DataLength;

		if(!(Header = (UCHAR*) NdisGetDataBuffer(nbl->FirstNetBuffer, min(FrameLength, sizeof(Frame)), Frame, 1, 1)))
		{
			LOG(LDBG, "Failed to get NDIS data buffer");
		}

		else
		{
			USHORT EtherType = *(USHORT*) (Header+12);

			if(EtherType != _byteswap_ushort(0x0800))
				LOG(LDBG, "Ignoring non-IP frame (0x%X)", EtherType);

			else
			{
				UINT32 SrcAddr = *(UINT32*) (Header+26);
				ULONG Processor = KeGetCurrentProcessorNumber();

				char tmp[32];
				ipxs(tmp, 32, SrcAddr);

				LOG(LDBG, "Receive (%u) from %s on %u", FrameLength, tmp, Processor);
			}
		}
	}

	NdisFIndicateReceiveNetBufferLists(Context->FilterModuleHandle, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
}





NTSTATUS RequestDispatch
(
	PDEVICE_OBJECT DeviceObject, 
	PIRP Irp
)
{
	PIO_STACK_LOCATION IrpStack;
	NTSTATUS Status = STATUS_SUCCESS;


	UNREFERENCED_PARAMETER(DeviceObject);

	IrpStack = IoGetCurrentIrpStackLocation(Irp);



	LOG(LDBG, "IoRequest Major: %u", IrpStack->MajorFunction);

	Irp->IoStatus.Status = Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Status;
}


VOID DriverUnload
(
	PDRIVER_OBJECT DriverObject
)
{
	UNREFERENCED_PARAMETER(DriverObject);


	LOG(LINFO, "Driver unloading");

	if(FltDeviceHandle != NULL)
		NdisDeregisterDeviceEx(FltDeviceHandle);

	if(FltDriverHandle)
		NdisFDeregisterFilterDriver(FltDriverHandle);

	FltDeviceObject = NULL;
	FltDeviceHandle = NULL;
	FltDriverHandle = NULL;
}


NTSTATUS DriverEntry
(
	PDRIVER_OBJECT DriverObject,
	PUNICODE_STRING RegistryPath
)
{
	NTSTATUS Status;
	NDIS_FILTER_DRIVER_CHARACTERISTICS Desc;
	UNICODE_STRING DevName;
	UNICODE_STRING DevSymName;
	NDIS_DEVICE_OBJECT_ATTRIBUTES DevAttr;
	PDRIVER_DISPATCH DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];

	UNREFERENCED_PARAMETER(RegistryPath);
	LOG(LINFO, "Driver entry -- 2");

	KeInitializeSpinLock(&DriverLock);
	ExInitializeSListHead(&FltModuleFreeList);
	ExInitializeSListHead(&FltModuleList);

	for(INT i = ARRAYSIZE(FltModules)-1; i >= 0; i--)
		ExInterlockedPushEntrySList(&FltModuleFreeList, &FltModules[i].Next, &DriverLock);

	// Install driver unload handler.
	DriverObject->DriverUnload = &DriverUnload;

	// Register ourselves with NDIS as a filter.
	NdisZeroMemory(&Desc, sizeof(Desc));
	Desc.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
	Desc.Header.Size = sizeof(Desc);
	Desc.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_2;
	Desc.MajorNdisVersion = 6;
	Desc.MinorNdisVersion = 50;
	Desc.MajorDriverVersion = 1;
	Desc.MinorDriverVersion = 0;
	Desc.Flags = 0;
	Desc.FriendlyName = RTL_CONSTANT_STRING(FLT_FRIENDLY_NAME);
	Desc.ServiceName = RTL_CONSTANT_STRING(FLT_SERVICE_NAME);
	Desc.UniqueName = RTL_CONSTANT_STRING(FLT_UNIQUE_NAME);

	Desc.AttachHandler = FilterAttach;
	Desc.DetachHandler = FilterDetach;
	Desc.PauseHandler = FilterPause;
	Desc.RestartHandler = FilterRestart;
	Desc.ReceiveNetBufferListsHandler = FilterReceive;

	if((Status = NdisFRegisterFilterDriver(DriverObject, (NDIS_HANDLE) DriverObject, &Desc, &FltDriverHandle)) != NDIS_STATUS_SUCCESS)
	{
		LOG(LFATAL, "Failed to register filter: 0x%X", Status);
		goto done;
	}

	// Create our device object for usermode communication.
	{
		NdisInitUnicodeString(&DevName, NTDEV_NAME);
		NdisInitUnicodeString(&DevSymName, NTDEV_SYMLINK);

		NdisZeroMemory(&DevAttr, sizeof(DevAttr));
		DevAttr.Header.Type = NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
		DevAttr.Header.Size = sizeof(DevAttr);
		DevAttr.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
		DevAttr.DeviceName = &DevName;
		DevAttr.SymbolicName = &DevSymName;
		DevAttr.MajorFunctions = DispatchTable;
		DevAttr.ExtensionSize = 0;

		NdisZeroMemory(&DispatchTable, sizeof(DispatchTable));
		DispatchTable[IRP_MJ_CREATE] = RequestDispatch;
		DispatchTable[IRP_MJ_CLEANUP] = RequestDispatch;
		DispatchTable[IRP_MJ_CLOSE] = RequestDispatch;
		DispatchTable[IRP_MJ_DEVICE_CONTROL] = RequestDispatch;

		if((Status = NdisRegisterDeviceEx(FltDriverHandle, &DevAttr, &FltDeviceObject, &FltDeviceHandle)) != NDIS_STATUS_SUCCESS)
		{
			LOG(LFATAL, "Failed to register device");
			NdisFDeregisterFilterDriver(FltDriverHandle);
		}

		LOG(LDBG, "Registered device");
	}

	LOG(LINFO, "Driver successfully loaded.");

done:
	return Status;
}




UINT8 ipxs(char* pd, UINT sz, UINT ip4)
{
	char* ptmp = pd;
	UINT8 o;

	UNREFERENCED_PARAMETER(sz);

	if(!ip4)
	{
		*((UINT64*) pd) = 0x00302E302E302E30;
		return 7;
	}

	o = (ip4&0xFF);

	for(INT i = 0; i < 4; i++)
	{
		if((o / 100))
		{
			*pd++ = '0' + (o/100);
			o -= (o/100)*100;

			*pd++ = '0' + (o/10);
			o -= (o/10)*10;
		}

		else if((o / 10))
		{
			*pd++ = '0' + (o/10);
			o -= (o/10)*10;
		}

		*pd++ = '0' + o;
		*pd++ = '.';

		ip4 >>= 8;
		o = (ip4 & 0xFF);
	}

	*--pd = '\0';

	return (UINT8) (pd-ptmp);
}