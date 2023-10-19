/*
 * driver.cpp
 *
 */
#include <ndis.h>
#include "shared.h"
#include "config.h"
#include <ntfe\ntfeusr.h>
#include <ntfe\eal.h>
#pragma warning(disable:4706) // Assignment within conditional





// Filter states
enum FILTER_STATE
{
	FilterStateUndefined,
	FilterStateAttached,
	FilterStatePausing,
	FilterStatePaused,
	FilterStateRestarting,
	FilterStateSuspended,
	FilterStateRunning,
	FilterStateDetaching,
	FilterStateDetached,
};

static const char* FILTER_STATE_TXT[] =
{
	"FilterStateUndefined",
	"FilterStateAttached",
	"FilterStatePausing",
	"FilterStatePaused",
	"FilterStateRestarting",
	"FilterStateSuspended",
	"FilterStateRunning",
	"FilterStateDetaching",
	"FilterStateDetached",
};


// Supported media of the lower-edge. This must match
// the binding as specified in the INF.
const NDIS_MEDIUM SupportedNdisMedia[] =
{
	NdisMedium802_3,
	NdisMediumWan, /* Is really just 802.3 */
};

// Filter instance (module) context.
struct FLT_MODULE
{
	union
	{
		LIST_ENTRY FltListLink; /* Module list link. */
		SINGLE_LIST_ENTRY FreeNext; /* Module freelist link. */
	};

	NDIS_HANDLE FilterModuleHandle;
	FILTER_STATE State;
};


// Prototypes
NTSTATUS ConfigureFirewallEngine();

// Globals
NDIS_HANDLE FltDriverHandle;
PDEVICE_OBJECT FltDeviceObject;
NDIS_HANDLE FltDeviceHandle;
NDIS_HANDLE NdisCfgHandle;

// All of our filter modules & freelist.
FLT_MODULE FltModules[MAX_FILTER_MODULES];
LIST_ENTRY FltModuleList;
SINGLE_LIST_ENTRY FltModuleFreeList;

// This is a count of the number of modules which are currently in the 'Running' state.
ULONG RunningModuleCount;

// Used to direct modules to enter the 'Suspended' state. The suspended state conceptually
// sits between 'Restarting' and 'Running'. Specifically, it indicates the module is 
// currently pending the restart(ing) request from NDIS.
BOOLEAN SuspendSignal;

// Signalled when the number of running modules ('RunningModuleCount') reaches zero.
// Note that, modules need not necessarily be in the 'Suspended' state (they may be
// in the 'Paused' state also) however that is the use-case.
KEVENT SuspendCompleteEvent;

// Serializing user IRPs.
KMUTEX UserMutex;

// A big lock; serializes access for control operations (very infrequent)
NDIS_SPIN_LOCK DriverLock;

#define ACQUIRE_DRIVER_LOCK() do { \
	NdisAcquireSpinLock(&DriverLock); \
	} while(0)

#define RELEASE_DRIVER_LOCK() do { \
	NdisReleaseSpinLock(&DriverLock); \
	} while(0)


// Helpers
#define GetModuleId(pModule) (ULONG) ((pModule - FltModules))



VOID SetModuleState(FLT_MODULE* Module, FILTER_STATE NewState)
{
	LDBG("[%u] %s -> %s", GetModuleId(Module), FILTER_STATE_TXT[Module->State], FILTER_STATE_TXT[NewState]);

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
			LWARN("Unable to attach; unsupported media.");
			Status = NDIS_STATUS_FAILURE;
			goto done;
		}
	}


	LOG("Attaching to %S; BaseMiniportIfIndex:%u IfIndex:%u LowerIfIndex:%u", AttachParameters->BaseMiniportName->Buffer, AttachParameters->BaseMiniportIfIndex, AttachParameters->IfIndex, AttachParameters->LowerIfIndex);

	// Allocate filter context state/module.
	ACQUIRE_DRIVER_LOCK();

	if(!(FilterModule = (FLT_MODULE*) PopEntryList(&FltModuleFreeList)))
	{
		LERR("Unable to attach; filter allocation failed.");
		Status = NDIS_STATUS_FAILURE;
		goto done;
	}

	InsertTailList(&FltModuleList, &FilterModule->FltListLink);

	FilterModule = CONTAINING_RECORD(FilterModule, FLT_MODULE, FreeNext);
	FilterModule->FilterModuleHandle = NdisFilterHandle;

	SetModuleState(FilterModule, FilterStateAttached);

	RELEASE_DRIVER_LOCK();

	NdisZeroMemory(&FilterAttr, sizeof(FilterAttr));
	FilterAttr.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
	FilterAttr.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
	FilterAttr.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
	FilterAttr.Flags = 0;

	NDIS_DECLARE_FILTER_MODULE_CONTEXT(FLT_MODULE);

	if((Status = NdisFSetAttributes(NdisFilterHandle, (NDIS_HANDLE) FilterModule, &FilterAttr)) != NDIS_STATUS_SUCCESS)
	{
		LERR("Failed to set filter attributes.");
		Status = NDIS_STATUS_FAILURE;
		goto done;
	}

done:
	return Status;
}


VOID FilterDetach
(
	FLT_MODULE* Context
)
{
	ACQUIRE_DRIVER_LOCK();

	SetModuleState(Context, FilterStateDetached);

	RemoveEntryList(&Context->FltListLink);
	PushEntryList(&FltModuleFreeList, &Context->FreeNext);

	RELEASE_DRIVER_LOCK();
}


NDIS_STATUS FilterPause
(
	FLT_MODULE* Context,
	PNDIS_FILTER_PAUSE_PARAMETERS PauseParameters
)
{
	UNREFERENCED_PARAMETER(PauseParameters);


	ACQUIRE_DRIVER_LOCK();

	if(Context->State == FilterStateRunning)
	{
		RunningModuleCount--;

		if(RunningModuleCount == 0)
		{
			KeSetEvent(&SuspendCompleteEvent, 0, FALSE);
		}
	}

	SetModuleState(Context, FilterStatePaused);

	RELEASE_DRIVER_LOCK();

	return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS FilterRestart
(
	FLT_MODULE* Context,
	PNDIS_FILTER_RESTART_PARAMETERS RestartParameters
)
{
	NDIS_STATUS Status;

	UNREFERENCED_PARAMETER(RestartParameters);
	
	ACQUIRE_DRIVER_LOCK();

	SetModuleState(Context, FilterStateRestarting);

	if(SuspendSignal)
	{
		SetModuleState(Context, FilterStateSuspended);
		Status = NDIS_STATUS_PENDING;
	}

	else
	{
		RunningModuleCount++;
		SetModuleState(Context, FilterStateRunning);
		Status = NDIS_STATUS_SUCCESS;
	}

	RELEASE_DRIVER_LOCK();

	return Status;
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
	KIRQL PrevIrql;
	ULONG AcceptCount = 0;
	PNET_BUFFER_LIST NblDropHead = NULL;
	PNET_BUFFER_LIST NblDropTail = (PNET_BUFFER_LIST) &NblDropHead;
	PNET_BUFFER_LIST NblAcceptHead = NULL;
	PNET_BUFFER_LIST NblAcceptTail = (PNET_BUFFER_LIST) &NblAcceptHead;
	XSTATE_SAVE SaveState;
	UCHAR FrameData[64];

	LDBG("[%u] FilterReceive, flags: 0x%X; ModuleHandle: 0x%X", GetModuleId(Context), ReceiveFlags, Context->FilterModuleHandle);


	NdisFIndicateReceiveNetBufferLists(Context->FilterModuleHandle, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);

	LDBG("-- Passthrough --");
	return;


	// We aren't handling this case for now; if the lower-edge is starved of resources, then terminate
	// the data-path here releasing resources back. It's assumed this is exceedingly rare given that
	// the miniport should be (or nearly) directly below us, and the presence of this flag presumably 
	// indicates impending exhaustion of the rx ring(s)
	//
	// TODO: Counter to keep an eye on this.
	if(ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES)
	{
		return;
	}

	// We must always be running at dispatch to prevent migration and/or preemption in the engine.
	if(!(ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL))
		PrevIrql = KeRaiseIrqlToDpcLevel();

	// Indicate to ntfe that a batch is about to begin. This should be performed as early as possible
	// to improve the effectiveness of overlapping of memory latency.
	//
	// N.B: It is crucial this is invoked only once we're at dispatch level.
	//
	ntfe_rx_prepare();

	// Store AVX state.
	if(KeSaveExtendedProcessorState(XSTATE_MASK_GSSE, &SaveState) != STATUS_SUCCESS)
	{
		// This shouldn't happen. Just transparently pass through to the next layer.
		NdisFIndicateReceiveNetBufferLists(Context->FilterModuleHandle, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
		goto restore_irql;
	}
	
	// Process each packet in the batch, segregating them into two seperate lists depending
	// on their fate (accept or drop)
	//
	// TODO: Investigate the performance ramifications of pipelining the processing into stages.
	//       
	//       Stage 1) Subdivide the batch into ~4 and perform NdisGetDataBuffer; the idea being 
	//                to make use of hot datastructures the kernel is touching.
	//       Stage 2) Process up to the point within the engine where bucket prefetching begins.
	//       Stage 3) Complete rx processing of batch. Hopefully by which time the first bucket
	//                has been brought into cache.
	//
	for(PNET_BUFFER_LIST nbl = NetBufferLists, Next; nbl; nbl = Next)
	{
		auto FrameLength = nbl->FirstNetBuffer->DataLength;
		auto HeaderPtr = NdisGetDataBuffer(nbl->FirstNetBuffer, min(FrameLength, sizeof(FrameData)), FrameData, 1, 1);

		if(!HeaderPtr || ntfe_rx(HeaderPtr, (u16) FrameLength))
		{
			NblAcceptTail->Next = nbl;
			NblAcceptTail = nbl;
			AcceptCount++;
		}

		else
		{
			NblDropTail->Next = nbl;
			NblDropTail = nbl;
		}

		Next = nbl->Next;
		nbl->Next = NULL;
	}

	// Restore AVX state.
	KeRestoreExtendedProcessorState(&SaveState);

restore_irql:
	// Restore IRQL if was modified by us.
	if(!(ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL))
		KeLowerIrql(PrevIrql);

	// TODO:
	ReceiveFlags &= ~NDIS_RECEIVE_FLAGS_PERFECT_FILTERED;

	// Release back the buffers for dropped packets.
	if(NblDropHead)
		NdisFReturnNetBufferLists(Context->FilterModuleHandle, NblDropHead, ReceiveFlags);

	// Pass up the accepted to the next layer.
	if(NblAcceptHead)
		NdisFIndicateReceiveNetBufferLists(Context->FilterModuleHandle, NblAcceptHead, PortNumber, AcceptCount, ReceiveFlags);
}


VOID FilterSend
(
	NDIS_HANDLE         FilterModuleContext,
	PNET_BUFFER_LIST    NetBufferLists,
	NDIS_PORT_NUMBER    PortNumber,
	ULONG               SendFlags
)
{
	FLT_MODULE* Context = (FLT_MODULE*) FilterModuleContext;
	KIRQL PrevIrql;
	XSTATE_SAVE SaveState;
	UCHAR FrameData[64];

	LDBG("[%u] FilterSend, flags: 0x%X ModuleHandle: 0x%X", GetModuleId(Context), SendFlags, Context->FilterModuleHandle);

	NdisFSendNetBufferLists(Context->FilterModuleHandle, NetBufferLists, PortNumber, SendFlags);

	LDBG("-- Passthrough --");
	return;


	// We must always be running at dispatch to prevent migration and/or preemption in the engine.
	if(!(SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL))
		PrevIrql = KeRaiseIrqlToDpcLevel();

	// Indicate to ntfw that we're about to begin a batch.
	ntfe_tx_prepare();

	// Store AVX state.
	if(KeSaveExtendedProcessorState(XSTATE_MASK_GSSE, &SaveState) != STATUS_SUCCESS)
		goto restore_irql;

	// Unlike the receive path, we don't perform transmit filtering; just pass them
	// through ntfe for inspection.
	for(PNET_BUFFER_LIST nbl = NetBufferLists; nbl; nbl = nbl->Next)
	{
		for(NET_BUFFER* nb = nbl->FirstNetBuffer; nb; nb = nb->Next)
		{
			auto FrameLength = nb->DataLength;
			auto HeaderPtr = NdisGetDataBuffer(nb, min(FrameLength, sizeof(FrameData)), FrameData, 1, 1);

			if(!HeaderPtr)
				continue;

			ntfe_tx(HeaderPtr, (u16) FrameLength);
		}
	}

	// Restore AVX state.
	KeRestoreExtendedProcessorState(&SaveState);

restore_irql:
	// Restore IRQL if it was modified by us.
	if(!(SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL))
		KeLowerIrql(PrevIrql);

	// Passthrough the NBLs untouched.
	NdisFSendNetBufferLists(Context->FilterModuleHandle, NetBufferLists, PortNumber, SendFlags);	
}


//
// Suspends all filtering, waiting if necessary.
//
// Upon return, `Suspend()` guarentees that all current and future filter modules
// within the driver are in the <= 'Suspended' state. Once suspended, 'Resume()`
// must be called to restore filtering.
// 
// IRQL: <= APC
//
// This routine cannot be called within the context of an NDIS callback.
//
VOID Suspend()
{
	ACQUIRE_DRIVER_LOCK();

	// Once we raise this under the driver lock we're guarenteed no further modules
	// can transition to 'Running'. They will all pend the the 'Restart' request
	// from NDIS of which will only complete upon resumption (via 'Resume()')
	//
	// In theory there could be an arbitrarily large number of filters on their way 
	// up due to some sort of Attach storm, however there's likely none at all except
	// those currently running we're about to restart.
	SuspendSignal = TRUE;

	if(RunningModuleCount != 0)
	{
		KeClearEvent(&SuspendCompleteEvent);

		// Initiate a restart of currently running modules.
		for(PLIST_ENTRY Link = FltModuleList.Flink; Link != &FltModuleList; Link = Link->Flink)
		{
			FLT_MODULE* Module = CONTAINING_RECORD(Link, FLT_MODULE, FltListLink);

			if(Module->State == FilterStateRunning)
			{
				LDBG("Restarting module %u", GetModuleId(Module));
				NdisFRestartFilter(Module->FilterModuleHandle);
			}
		}
	}

	RELEASE_DRIVER_LOCK();

	// Wait for all modules which are running to transition to, minimally, the 
	// 'Pausing' state and at most 'Suspended'
	KeWaitForSingleObject(&SuspendCompleteEvent, Executive, KernelMode, FALSE, NULL);
}


VOID Resume()
{
	ACQUIRE_DRIVER_LOCK();

	// Lower the suspension signal.
	SuspendSignal = FALSE;

	// Iterate over all the suspended modules; these will all have a pending restart
	// operation we now need to complete.
	for(PLIST_ENTRY Link = FltModuleList.Flink; Link != &FltModuleList; Link = Link->Flink)
	{
		FLT_MODULE* Module = CONTAINING_RECORD(Link, FLT_MODULE, FltListLink);

		LDBG("Resume(): Module[%u] state: %s", GetModuleId(Module), FILTER_STATE_TXT[Module->State]);

		if(Module->State == FilterStateSuspended)
		{
			LDBG("Completing restart for module: %u", GetModuleId(Module));
			NdisFRestartComplete(Module->FilterModuleHandle, NDIS_STATUS_SUCCESS);
		}
	}

	RELEASE_DRIVER_LOCK();
}


NTSTATUS RequestDispatch
(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp
)
{
	PIO_STACK_LOCATION IrpSp;
	NTSTATUS Status = STATUS_SUCCESS;


	UNREFERENCED_PARAMETER(DeviceObject);

	IrpSp = IoGetCurrentIrpStackLocation(Irp);


	// Only interested in IOCTLs.
	if(IrpSp->MajorFunction != IRP_MJ_DEVICE_CONTROL)
		goto done;
	
	// Verify we're not at dispatch as we need to yield for synchronization.
	if(KeGetCurrentIrql() >= DISPATCH_LEVEL)
	{
		// We can't log this, as logging itself may require <= APC.
		Status = STATUS_INTERNAL_ERROR;
		goto done;
	}

	// All our operations are synchronous and there should never be more than
	// one request outstanding.
	KeWaitForSingleObject(&UserMutex, Executive, KernelMode, FALSE, NULL);

	switch(IrpSp->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_NTFW_ENGINE_RESTART:
		{
			// Bring all filters to a stop, apply any new configuration and restore.
			Suspend();
			Status = ConfigureFirewallEngine();
			Resume();

		} break;

		default:
			Status = STATUS_INVALID_DEVICE_REQUEST;
	}

	// Exit user region.
	KeReleaseMutex(&UserMutex, FALSE);


done:
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

	LOG("Driver unloading");

	ntfe_cleanup();

	if(NdisCfgHandle != NULL)
		NdisCloseConfiguration(NdisCfgHandle);

	if(FltDeviceHandle != NULL)
		NdisDeregisterDeviceEx(FltDeviceHandle);

	if(FltDriverHandle != NULL)
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
	NDIS_CONFIGURATION_OBJECT CfgObj;
	NDIS_FILTER_DRIVER_CHARACTERISTICS Desc;
	UNICODE_STRING DevName;
	UNICODE_STRING DevSymName;
	NDIS_DEVICE_OBJECT_ATTRIBUTES DevAttr;
	PDRIVER_DISPATCH DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];

	UNREFERENCED_PARAMETER(RegistryPath);

	LOG("ntfwkm driver entry.");

	// Basic initialization.
	NdisAllocateSpinLock(&DriverLock);
	KeInitializeEvent(&SuspendCompleteEvent, NotificationEvent, FALSE);
	KeInitializeMutex(&UserMutex, 0);
	InitializeListHead(&FltModuleList);

	for(INT i = ARRAYSIZE(FltModules)-1; i >= 0; i--)
		PushEntryList(&FltModuleFreeList, &FltModules[i].FreeNext);

	if(!ntfe_init())
	{
		LERR("Firewall engine initialization failed.");
		return STATUS_INTERNAL_ERROR;
	}

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
	Desc.FriendlyName = RTL_CONSTANT_STRING(NTFW_FRIENDLY_NAME);
	Desc.ServiceName = RTL_CONSTANT_STRING(NTFW_SERVICE_NAME);
	Desc.UniqueName = RTL_CONSTANT_STRING(NTFW_UNIQUE_NAME);

	Desc.AttachHandler = FilterAttach;
	Desc.DetachHandler = (FILTER_DETACH_HANDLER) FilterDetach;
	Desc.PauseHandler = (FILTER_PAUSE_HANDLER) FilterPause;
	Desc.RestartHandler = (FILTER_RESTART_HANDLER) FilterRestart;
	Desc.ReceiveNetBufferListsHandler = FilterReceive;
	Desc.SendNetBufferListsHandler = FilterSend;

	if((Status = NdisFRegisterFilterDriver(DriverObject, (NDIS_HANDLE) DriverObject, &Desc, &FltDriverHandle)) != NDIS_STATUS_SUCCESS)
	{
		LFATAL("Failed to register filter: 0x%X", Status);
		Status = STATUS_INTERNAL_ERROR;
		goto done;
	}

	CfgObj.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
	CfgObj.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
	CfgObj.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
	CfgObj.NdisHandle = FltDriverHandle;
	CfgObj.Flags = 0;

	if((Status = NdisOpenConfigurationEx(&CfgObj, &NdisCfgHandle)) != NDIS_STATUS_SUCCESS)
	{
		LFATAL("Unable to open driver configuration: 0x%X", Status);
		Status = STATUS_INTERNAL_ERROR;
		goto done;
	}
	
	// Create our device object for usermode communication.
	{
		NdisInitUnicodeString(&DevName, NTFW_DEV_NAME);
		NdisInitUnicodeString(&DevSymName, NTFW_DEV_SYMLINK);

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
			NdisFDeregisterFilterDriver(FltDriverHandle);

			LOG("Failed to register device");
			Status = STATUS_INTERNAL_ERROR;
			goto done;
		}
	}

	LOG("ntfwkm sucessfully initialized.");
	Status = STATUS_SUCCESS;

done:
	return Status;
}


NTSTATUS ConfigureFirewallEngine()
{
	NDIS_STATUS Status = STATUS_SUCCESS;
	HANDLE FileHandle = NULL;
	PVOID FileData = NULL;
	NDIS_STRING ValueName;
	OBJECT_ATTRIBUTES ObjAttr;
	PNDIS_CONFIGURATION_PARAMETER Param;
	IO_STATUS_BLOCK IoStatus;
	FILE_STANDARD_INFORMATION FileInfo;


	// Read the user-configured path specifying the location of the configuration file (.ntfc)
	ValueName = RTL_CONSTANT_STRING(NTFW_REG_ENGINE_CFG_FILE);

	NdisReadConfiguration(&Status, &Param, NdisCfgHandle, &ValueName, NdisParameterString);

	if(Status != NDIS_STATUS_SUCCESS)
	{
		LERR("Unable to read configuration path registry value '%wZ': 0x%X", &ValueName, Status);
		goto done;
	}

	LOG("Reading engine configuration file '%wZ'", &Param->ParameterData.StringData);

	// Open the configuration file and read it in synchronously.
	InitializeObjectAttributes(&ObjAttr, &Param->ParameterData.StringData, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	if((Status = ZwOpenFile(&FileHandle, GENERIC_READ, &ObjAttr, &IoStatus, FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT)) != STATUS_SUCCESS)
	{
		LERR("Failed to open configuration file: 0x%X", Status);
		goto done;
	}

	if((Status = ZwQueryInformationFile(FileHandle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation)) != STATUS_SUCCESS)
	{
		LERR("Failed to query configuration file information: 0x%X", Status);
		goto done;
	}

	if(FileInfo.EndOfFile.QuadPart > MAX_NTFE_CFG_SZ)
	{
		LERR("Unexpectedly large configuration file size");
		Status = STATUS_FILE_TOO_LARGE;
		goto done;
	}

	if(!(FileData = ExAllocatePool(PagedPool, FileInfo.EndOfFile.QuadPart)))
	{
		LERR("Failed to allocate configuration file buffer.");
		goto done;
	}

	if((Status = ZwReadFile(FileHandle, NULL, NULL, NULL, &IoStatus, FileData, FileInfo.EndOfFile.LowPart, NULL, NULL)) != STATUS_SUCCESS)
	{
		LERR("Failed to read configuration file: 0x%X", Status);
		goto done;
	}

	LOG("Successfully read configuration file: %u bytes", IoStatus.Information);

	// Apply the configuration.
	if(!ntfe_validate_configuration((ntfe_config*) FileData, FileInfo.EndOfFile.LowPart))
	{
		LERR("Corrupt or malformed configuration data.");
		Status = STATUS_IO_DEVICE_INVALID_DATA;
		goto done;
	}

	if(!ntfe_configure((ntfe_config*) FileData, FileInfo.EndOfFile.LowPart))
	{
		LERR("Failed to configure engine.");
		Status = STATUS_INTERNAL_ERROR;
		goto done;
	}

done:
	if(FileHandle)
		ZwClose(FileHandle);

	if(FileData)
		ExFreePool(FileData);

	return Status;
}