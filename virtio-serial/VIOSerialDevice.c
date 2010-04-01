/**********************************************************************
 * Copyright (c) 2010  Red Hat, Inc.
 *
 * File: VIOSerialDevice.c
 *
 * Placeholder for the device related functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
**********************************************************************/

#include "osdep.h"
#include "kdebugprint.h"
#include "VIOSerialDriver.h"
#include "VIOSerialDevice.h"
#include "VIOSerialCore.h"


// Break huge add device into chunks
static void VIOSerialInitPowerManagement(IN PWDFDEVICE_INIT DeviceInit,
										 IN WDF_PNPPOWER_EVENT_CALLBACKS *stPnpPowerCallbacks);
static void VIOSerialInitFileObject(IN PWDFDEVICE_INIT DeviceInit,
									WDF_FILEOBJECT_CONFIG * pFileCfg);
static NTSTATUS VIOSerialInitIO(WDFDEVICE hDevice);
static void VIOSerialInitDeviceContext(WDFDEVICE hDevice);
static NTSTATUS VIOSerialInitInterruptHandling(WDFDEVICE hDevice);

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VIOSerialEvtDeviceAdd)
#pragma alloc_text (PAGE, VIOSerialEvtDevicePrepareHardware)
#pragma alloc_text (PAGE, VIOSerialEvtDeviceReleaseHardware)

#pragma alloc_text (PAGE, VIOSerialInitPowerManagement)
#pragma alloc_text (PAGE, VIOSerialInitFileObject)
#pragma alloc_text (PAGE, VIOSerialInitIO)
#pragma alloc_text (PAGE, VIOSerialInitDeviceContext)

//#pragma alloc_text (PAGE, VIOSerialEvtIoRead)
//#pragma alloc_text (PAGE, VIOSerialEvtIoWrite)
//#pragma alloc_text (PAGE, VIOSerialEvtIoDeviceControl)
#endif


static void VIOSerialInitPowerManagement(IN PWDFDEVICE_INIT DeviceInit,
										 IN WDF_PNPPOWER_EVENT_CALLBACKS *stPnpPowerCallbacks)
{
	PAGED_CODE();
	DEBUG_ENTRY(0);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(stPnpPowerCallbacks);
	stPnpPowerCallbacks->EvtDevicePrepareHardware = VIOSerialEvtDevicePrepareHardware;
	stPnpPowerCallbacks->EvtDeviceReleaseHardware = VIOSerialEvtDeviceReleaseHardware;
	stPnpPowerCallbacks->EvtDeviceD0Entry = VIOSerialEvtDeviceD0Entry;
	stPnpPowerCallbacks->EvtDeviceD0Exit = VIOSerialEvtDeviceD0Exit;

	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, stPnpPowerCallbacks);
}

static void VIOSerialInitFileObject(IN PWDFDEVICE_INIT DeviceInit,
									WDF_FILEOBJECT_CONFIG * pFileCfg)
{
	PAGED_CODE();
	DEBUG_ENTRY(0);

// Create file object to handle Open\Close events
	WDF_FILEOBJECT_CONFIG_INIT(pFileCfg,
							   VIOSerialEvtDeviceFileCreate,
							   VIOSerialEvtFileClose,
							   WDF_NO_EVENT_CALLBACK);

	WdfDeviceInitSetFileObjectConfig(DeviceInit,
									 pFileCfg,
									 WDF_NO_OBJECT_ATTRIBUTES);

}

static NTSTATUS VIOSerialInitIO(WDFDEVICE hDevice)
{
	WDF_IO_QUEUE_CONFIG		queueCfg;
	WDF_IO_QUEUE_CONFIG		queueReadCfg;
	WDF_IO_QUEUE_CONFIG		queueWriteCfg;
	NTSTATUS				status;
	WDFQUEUE				queue;
	WDFQUEUE				queueRead;
	WDFQUEUE				queueWrite;
	DECLARE_CONST_UNICODE_STRING(strVIOSerialSymbolicLink, VIOSERIAL_SYMBOLIC_LINK);

	PAGED_CODE();
	DEBUG_ENTRY(0);

	/*
	TBD - after initial implementation change to raw mode.
The mode of operation in which a device's driver stack does not include a function driver. A device running in raw mode is being controlled primarily by the bus driver. Upper-level, lower-level, and/or bus filter drivers might be included in the driver stack. 
If a bus driver can control a device in raw mode, it sets RawDeviceOK in the DEVICE_CAPABILITIES structure.
	*/

	//Create symbolic link to represent the device in the system
	if (!NT_SUCCESS(status = WdfDeviceCreateSymbolicLink(hDevice, &strVIOSerialSymbolicLink)))
	{
		DPrintf(0, ("WdfDeviceCreateSymbolicLink failed - 0x%x\n", status));
		return status;
	}

	//Create the IO queue to handle IO requests
	// TDB - check if parallel mode is more apropreate
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueCfg, WdfIoQueueDispatchSequential);
	WDF_IO_QUEUE_CONFIG_INIT(&queueReadCfg, WdfIoQueueDispatchSequential);
	WDF_IO_QUEUE_CONFIG_INIT(&queueWriteCfg, WdfIoQueueDispatchSequential);

	queueCfg.EvtIoDeviceControl = VIOSerialEvtIoDeviceControl;
	queueReadCfg.EvtIoRead = VIOSerialEvtIoRead;
	queueWriteCfg.EvtIoWrite = VIOSerialEvtIoWrite;

	status = WdfIoQueueCreate(hDevice,
							  &queueCfg,
							  WDF_NO_OBJECT_ATTRIBUTES,
							  &queue
							  );

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfIoQueueCreate failed - 0x%x\n", status));
		return status;
	}

	status = WdfIoQueueCreate(hDevice,
							  &queueReadCfg,
							  WDF_NO_OBJECT_ATTRIBUTES,
							  &queueRead
							  );

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfIoQueueCreate for read failed - 0x%x\n", status));
		return status;
	}

	status = WdfIoQueueCreate(hDevice,
							  &queueWriteCfg,
							  WDF_NO_OBJECT_ATTRIBUTES,
							  &queueWrite
							  );

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfIoQueueCreate for write failed - 0x%x\n", status));
		return status;
	}

	status = WdfDeviceConfigureRequestDispatching(hDevice,
												  queueRead,
												  WdfRequestTypeRead);

	if(!NT_SUCCESS(status))
	{
		DPrintf(0, ("WdfDeviceConfigureRequestDispatching read failed - 0x%x\n", status));
		return status;
	}

	status = WdfDeviceConfigureRequestDispatching(hDevice,
												  queueWrite,
												  WdfRequestTypeWrite);

	if(!NT_SUCCESS(status))
	{
		DPrintf(0, ("WdfDeviceConfigureRequestDispatching write failed - 0x%x\n", status));
		return status;
	}

	return STATUS_SUCCESS;
}

static void VIOSerialInitDeviceContext(WDFDEVICE hDevice)
{
	PDEVICE_CONTEXT	pContext;
	int i;

	PAGED_CODE();
	DEBUG_ENTRY(0);

	pContext = GetDeviceContext(hDevice);

	if(pContext)
	{
		memset(pContext, 0, sizeof(DEVICE_CONTEXT));
		//Init Spin locks
		WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES,
						  &pContext->DPCLock);

		for(i = 0; i < VIRTIO_SERIAL_MAX_QUEUES_COUPLES; i++)
		{
			InitializeListHead(&pContext->SerialPorts[i].ReceiveBuffers);
			InitializeListHead(&pContext->SerialPorts[i].SendFreeBuffers);
			InitializeListHead(&pContext->SerialPorts[i].SendInUseBuffers);
		}
	}
}

static NTSTATUS VIOSerialInitInterruptHandling(WDFDEVICE hDevice)
{
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_INTERRUPT_CONFIG interruptConfig;
	PDEVICE_CONTEXT	pContext = GetDeviceContext(hDevice);
	NTSTATUS status = STATUS_SUCCESS;

	DEBUG_ENTRY(0);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
	WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
							  VIOSerialInterruptIsr,
							  VIOSerialInterruptDpc);

	interruptConfig.EvtInterruptEnable = VIOSerialInterruptEnable;
	interruptConfig.EvtInterruptDisable = VIOSerialInterruptDisable;

	status = WdfInterruptCreate(hDevice,
								&interruptConfig,
								&attributes,
								&pContext->WdfInterrupt);

	if (!NT_SUCCESS (status))
	{
		DPrintf(0, ("WdfInterruptCreate failed: %x\n", status));
		return status;
	}

	return status;
}

/////////////////////////////////////////////////////////////////////////////////
//
// VIOSerialEvtDeviceAdd
//
// Called by WDF framework as a callback for AddDevice from PNP manager.
// New device object instance should be initialized here
//
/////////////////////////////////////////////////////////////////////////////////
NTSTATUS VIOSerialEvtDeviceAdd(IN WDFDRIVER Driver,IN PWDFDEVICE_INIT DeviceInit)
{
	NTSTATUS						status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES			fdoAttributes;
	WDFDEVICE						hDevice;
	WDF_PNPPOWER_EVENT_CALLBACKS	stPnpPowerCallbacks;
	WDF_FILEOBJECT_CONFIG			fileCfg;
	
	UNREFERENCED_PARAMETER(Driver);
	PAGED_CODE();
	DEBUG_ENTRY(0);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, DEVICE_CONTEXT);
	VIOSerialInitPowerManagement(DeviceInit, &stPnpPowerCallbacks);
	VIOSerialInitFileObject(DeviceInit, &fileCfg);

	if (!NT_SUCCESS(status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &hDevice)))
	{
		DPrintf(0, ("WdfDeviceCreate failed - 0x%x\n", status));
		return status;
	}

	VIOSerialInitDeviceContext(hDevice);

	if(!NT_SUCCESS(status = VIOSerialInitIO(hDevice)))
	{
		return status;
	}

	if(!NT_SUCCESS(status = VIOSerialInitInterruptHandling(hDevice)))
	{
		return status;
	}

	return status;
}

/////////////////////////////////////////////////////////////////////////////////
//
// VIOSerialEvtDevicePrepareHardware
//
// Init virtio interface for usage
//
/////////////////////////////////////////////////////////////////////////////////
NTSTATUS VIOSerialEvtDevicePrepareHardware(IN WDFDEVICE Device,
										   IN WDFCMRESLIST ResourcesRaw,
										   IN WDFCMRESLIST ResourcesTranslated)
{
	int nListSize = 0;
	PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor;
	int i = 0;
	PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
	bool bPortFound = FALSE;
	NTSTATUS status = STATUS_SUCCESS;

	DEBUG_ENTRY(0);

	nListSize = WdfCmResourceListGetCount(ResourcesTranslated);

	for (i = 0; i < nListSize; i++)
	{
		if(pResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i))
		{
			switch(pResDescriptor->Type)
			{
				case CmResourceTypePort:
					pContext->bPortMapped =
							(pResDescriptor->Flags & CM_RESOURCE_PORT_IO) ? FALSE : TRUE;

					pContext->PortBasePA = pResDescriptor->u.Port.Start;
					pContext->uPortLength = pResDescriptor->u.Port.Length;

					DPrintf(0, ("IO Port Info  [%08I64X-%08I64X]\n",
							pResDescriptor->u.Port.Start.QuadPart,
							pResDescriptor->u.Port.Start.QuadPart +
							pResDescriptor->u.Port.Length));

					if (pContext->bPortMapped ) // Port is IO mapped
					{
						pContext->pPortBase = MmMapIoSpace(pContext->PortBasePA,
														   pContext->uPortLength,
														   MmNonCached);

						if (!pContext->pPortBase) {
							DPrintf(0, ("%s>>> Failed to map IO port!\n", __FUNCTION__));
							return STATUS_INSUFFICIENT_RESOURCES;
						}
					}
					else // Memory mapped port
					{
						pContext->pPortBase = (PVOID)(ULONG_PTR)pContext->PortBasePA.QuadPart;
					}

					bPortFound = TRUE;

					break;
				///
				case CmResourceTypeInterrupt:
					// Print out interrupt info- debugging only
					DPrintf(0, ("Resource Type Interrupt\n"));
					DPrintf(0, ("Interrupt.Level %x\n", pResDescriptor->u.Interrupt.Level));
					DPrintf(0, ("Interrupt.Vector %x\n", pResDescriptor->u.Interrupt.Vector));
					DPrintf(0, ("Interrupt.Affinity %x\n", pResDescriptor->u.Interrupt.Affinity));

					break;
			}
		}
	}

	if(!bPortFound)
	{
		DPrintf(0, ("%s>>> %s", __FUNCTION__, "IO port wasn't found!\n"));
		return STATUS_DEVICE_CONFIGURATION_ERROR;
	}

	VSCInit(Device);
	
	pContext->isDeviceInitialized = TRUE;
	VSCGuestSetPortsReady(pContext);

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceReleaseHardware(IN WDFDEVICE Device,
										   IN WDFCMRESLIST ResourcesTranslated)
{
	PDEVICE_CONTEXT pContext = GetDeviceContext(Device);
	UNREFERENCED_PARAMETER(ResourcesTranslated);
	
	PAGED_CODE();
	
	DEBUG_ENTRY(0);
	
	VSCDeinit(Device);

	pContext->isDeviceInitialized = FALSE;
	
	if (pContext->pPortBase && pContext->bPortMapped) 
	{
		MmUnmapIoSpace(pContext->pPortBase, pContext->uPortLength);
	}

	pContext->pPortBase = (ULONG_PTR)NULL;

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Entry(IN WDFDEVICE Device, 
								   WDF_POWER_DEVICE_STATE  PreviousState)
{
	DEBUG_ENTRY(0);

	//TBD - "power up" the device

	return STATUS_SUCCESS;
}

NTSTATUS VIOSerialEvtDeviceD0Exit(IN WDFDEVICE Device, 
								  IN WDF_POWER_DEVICE_STATE TargetState)
{
	DEBUG_ENTRY(0);

	//TBD - "power down" the device

	return STATUS_SUCCESS;
}

static PDEVICE_CONTEXT GetContextFromQueue(IN WDFQUEUE Queue)
{
	PDEVICE_CONTEXT pContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));

	return pContext;
}

VOID VIOSerialEvtIoDeviceControl(IN WDFQUEUE Queue,
								 IN WDFREQUEST Request,
								 IN size_t OutputBufferLength,
								 IN size_t InputBufferLength,
								 IN ULONG  IoControlCode)
{
	DEBUG_ENTRY(0);

	/* Do we need to handle IOCTLs?*/
	WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID VIOSerialEvtRequestCancel(IN WDFREQUEST Request)
{
	PDEVICE_CONTEXT pContext = GetContextFromQueue(WdfRequestGetIoQueue(Request));
	
	DEBUG_ENTRY(0);

	WdfSpinLockAcquire(pContext->DPCLock);

	VIOSerialQueueRequest(pContext,
						  WdfRequestGetFileObject(Request),
						  NULL);
	WdfSpinLockRelease(pContext->DPCLock);

	WdfRequestComplete(Request, STATUS_CANCELLED);

	return;
}

VOID VIOSerialEvtIoRead(IN WDFQUEUE  Queue,
						IN WDFREQUEST Request,
						IN size_t Length)
{
	WDFMEMORY outMemory;
	size_t size = Length;
	PVOID buffer = NULL;
	NTSTATUS status;
	PDEVICE_CONTEXT pContext = GetContextFromQueue(Queue);

	DPrintf(0, ("%s> %d bytes", __FUNCTION__, Length));

	if(NT_SUCCESS(status = WdfRequestRetrieveOutputMemory(Request, &outMemory)))
	{
		status = VSCGetData(WdfRequestGetFileObject(Request), 
							pContext,
							&outMemory,
							&size);

		if(status != STATUS_UNSUCCESSFUL)
		{
			WdfRequestCompleteWithInformation(Request, status, size);
		}
		else  //There was no data to in queue, handle request when the data is ready
		{
			DPrintf(0, ("Mark read request pending %x\n", Request));

			WdfSpinLockAcquire(pContext->DPCLock);
			if(NT_SUCCESS(status = WdfRequestMarkCancelableEx(Request, VIOSerialEvtRequestCancel)))
			{
				VIOSerialQueueRequest(pContext, WdfRequestGetFileObject(Request), Request);
				WdfSpinLockRelease(pContext->DPCLock);
			}
			else
			{
				WdfSpinLockRelease(pContext->DPCLock);
				WdfRequestCompleteWithInformation (Request, status, 0);
			}
			
		}
	}
	else
	{
		WdfRequestCompleteWithInformation(Request, status, size);
	}
}

VOID VIOSerialEvtIoWrite(IN WDFQUEUE  Queue,
						 IN WDFREQUEST Request,
						 IN size_t Length)
{
	WDFMEMORY inMemory;
	size_t size = 0;
	PVOID buffer = NULL;
	NTSTATUS status;

	DEBUG_ENTRY(0);
	
	if(NT_SUCCESS(WdfRequestRetrieveInputMemory(Request, &inMemory)))
	{
		buffer = WdfMemoryGetBuffer(inMemory, &size);

		//Just for safety- checking that the buffer size of IRP is not smaller than
		// the size of the embedded memory object
		if(size > Length)
		{
			size = Length;
		}

		status = VSCSendData(WdfRequestGetFileObject(Request),
							 GetContextFromQueue(Queue),
							 buffer,
							 &size);
	}

	WdfRequestCompleteWithInformation(Request, status, size);
}


void VIOSerialEvtDeviceFileCreate(IN WDFDEVICE Device,
								  IN WDFREQUEST Request,
								  IN WDFFILEOBJECT FileObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	DEBUG_ENTRY(0);

	if(NT_SUCCESS(status = VSCGuestOpenedPort(FileObject, GetDeviceContext(Device))))
	{
		//TBD - do some stuff on the device level on file open if needed
	}

	WdfRequestComplete(Request, status);
}

VOID VIOSerialEvtFileClose(IN WDFFILEOBJECT FileObject)
{
	DEBUG_ENTRY(0);
	//Clean up on file close

	VSCGuestClosedPort(FileObject, GetContextFromFileObject(FileObject));
}