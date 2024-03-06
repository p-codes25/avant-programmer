/*
 *
 *	AvantFlt.h
 *
 *	Based on kbfiltr DDK sample, Copyright (c) 1997  Microsoft Corporation
 *
 *	This module contains the common private declarations for the keyboard
 *	packet filter driver.
 */

#ifndef KBFILTER_H
#define KBFILTER_H

#pragma warning(disable:4201)

#include "ntddk.h"
#include "kbdmou.h"
#include <ntddkbd.h>
#include <ntdd8042.h>

#pragma warning(default:4201)

#include <wdf.h>

#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#include <initguid.h>
#include <devguid.h>

#include "public.h"

#define KBFILTER_POOL_TAG (ULONG) 'tlfK'

#if DBG

#define TRAP()                      DbgBreakPoint()

#define DebugPrint(_x_) DbgPrint _x_

#else   // DBG

#define TRAP()

#define DebugPrint(_x_)

#endif

#define MIN(_A_,_B_) (((_A_) < (_B_)) ? (_A_) : (_B_))

typedef struct _DEVICE_EXTENSION
{
    WDFDEVICE WdfDevice;

    //
    // Queue for handling requests that come from the rawPdo
    //
    WDFQUEUE rawPdoQueue;

    //
    // Number of creates sent down
    //
    LONG EnableCount;

    //
    // The real connect data that this driver reports to
    //
    CONNECT_DATA UpperConnectData;

    //
    // Previous initialization and hook routines (and context)
    //
    PVOID UpperContext;
    PI8042_KEYBOARD_INITIALIZATION_ROUTINE UpperInitializationRoutine;
    PI8042_KEYBOARD_ISR UpperIsrHook;

    //
    // Write function from within KbFilter_IsrHook
    //
    IN PI8042_ISR_WRITE_PORT IsrWritePort;

    //
    // Queue the current packet (ie the one passed into KbFilter_IsrHook)
    //
    IN PI8042_QUEUE_PACKET QueueKeyboardPacket;

    //
    // Context for IsrWritePort, QueueKeyboardPacket
    //
    IN PVOID CallContext;

    //
    // Cached Keyboard Attributes
    //
    KEYBOARD_ATTRIBUTES KeyboardAttributes;

	// ISR state machine
	enum
	{
		I8042PGM_NO_REQUEST,				// 0 = No request in progress
		I8042PGM_WAIT_CMD_RESPONSE1,		// 1 = Waiting for initial (original) command byte response
		I8042PGM_WAIT_CMD_RESPONSE2,		// 2 = Waiting for new (confirmation) command byte response
		I8042PGM_WAIT_BYTE_RESPONSE,		// 3 = Waiting for keyboard response
		I8042PGM_WAIT_CMD_RESPONSE3,		// 4 = Waiting for old (confirmation) command byte response
		I8042PGM_INVALID
	} RequestState;

	// Parking spot for a WDFREQUEST to write a byte to the keyboard
	WDFREQUEST RequestInProgress;

	// DPC that we fire off when we receive the response to our keyboard write request, to complete the user-level ioctl
	WDFDPC IoctlCompleteDpc;

	// Byte to send to keyboard
	BYTE	cRequest;

	// Saved command byte, read before we turn off scan code translation so we can put it back afterward
	BYTE	cSaveCommandByte;

	// Our modified command byte
	BYTE	cOurCommandByte;

	// Response byte to our written byte -- this is returned to the ioctl
	BYTE	cResponse;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION,
                                        FilterGetData)


typedef struct _WORKER_ITEM_CONTEXT
{
    WDFREQUEST  Request;
    WDFIOTARGET IoTarget;

} WORKER_ITEM_CONTEXT, *PWORKER_ITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKER_ITEM_CONTEXT, GetWorkItemContext)

//
// Prototypes
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD KbFilter_EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KbFilter_EvtIoDeviceControlForRawPdo;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KbFilter_EvtIoDeviceControlFromRawPdo;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL KbFilter_EvtIoInternalDeviceControl;

NTSTATUS
KbFilter_InitializationRoutine(
    IN PVOID                           InitializationContext,
    IN PVOID                           SynchFuncContext,
    IN PI8042_SYNCH_READ_PORT          ReadPort,
    IN PI8042_SYNCH_WRITE_PORT         WritePort,
    OUT PBOOLEAN                       TurnTranslationOn
    );

BOOLEAN
KbFilter_IsrHook(
    PVOID                  IsrContext,
    PKEYBOARD_INPUT_DATA   CurrentInput,
    POUTPUT_PACKET         CurrentOutput,
    UCHAR                  StatusByte,
    PUCHAR                 DataByte,
    PBOOLEAN               ContinueProcessing,
    PKEYBOARD_SCAN_STATE   ScanState
    );

VOID
KbFilter_ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PKEYBOARD_INPUT_DATA InputDataStart,
    IN PKEYBOARD_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    );

EVT_WDF_REQUEST_COMPLETION_ROUTINE
KbFilterRequestCompletionRoutine;


typedef struct _RPDO_DEVICE_DATA
{

    ULONG InstanceNo;

    //
    // Queue of the parent device we will forward requests to
    //
    WDFQUEUE ParentQueue;

} RPDO_DEVICE_DATA, *PRPDO_DEVICE_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RPDO_DEVICE_DATA, PdoGetData)


NTSTATUS
KbFiltr_CreateRawPdo(
    WDFDEVICE       Device,
    ULONG           InstanceNo
);



#endif  // KBFILTER_H


