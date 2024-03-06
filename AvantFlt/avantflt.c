/*--

This was based on the DDK keyboard filter driver project (kbfiltr), which carries
this copyright notice and information:

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.


Abstract: This is an upper device filter driver sample for PS/2 keyboard. This
        driver layers in between the KbdClass driver and i8042prt driver and
        hooks the callback routine that moves keyboard inputs from the port
        driver to class driver. With this filter, you can remove or insert
        additional keys into the stream. This sample also creates a raw
        PDO and registers an interface so that application can talk to
        the filter driver directly without going thru the PS/2 devicestack.
        The reason for providing this additional interface is because the keyboard
        device is an exclusive secure device and it's not possible to open the
        device from usermode and send custom ioctls.

        If you want to filter keyboard inputs from all the keyboards (ps2, usb)
        plugged into the system then you can install this driver as a class filter
        and make it sit below the kbdclass filter driver by adding the service
        name of this filter driver before the kbdclass filter in the registry at
        " HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\
        {4D36E96B-E325-11CE-BFC1-08002BE10318}\UpperFilters"


Environment:

    Kernel mode only.

--*/

#include "avantflt.h"
#include "kbif.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, KbFilter_EvtDeviceAdd)
#pragma alloc_text (PAGE, KbFilter_EvtIoInternalDeviceControl)
#endif

ULONG InstanceNo = 0;

EVT_WDF_DPC KbFiltrCompleteIoctl;


NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG               config;
    NTSTATUS                        status;

    DebugPrint(("Keyboard Filter Driver Sample - Driver Framework Edition.\n"));
    DebugPrint(("Built %s %s\n", __DATE__, __TIME__));

    //
    // Initiialize driver config to control the attributes that
    // are global to the driver. Note that framework by default
    // provides a driver unload routine. If you create any resources
    // in the DriverEntry and want to be cleaned in driver unload,
    // you can override that by manually setting the EvtDriverUnload in the
    // config structure. In general xxx_CONFIG_INIT macros are provided to
    // initialize most commonly used members.
    //

    WDF_DRIVER_CONFIG_INIT(
        &config,
        KbFilter_EvtDeviceAdd
    );

    //
    // Create a framework driver object to represent our driver.
    //
    status = WdfDriverCreate(DriverObject,
                            RegistryPath,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &config,
                            WDF_NO_HANDLE); // hDriver optional
    if (!NT_SUCCESS(status)) {
        DebugPrint(("WdfDriverCreate failed with status 0x%x\n", status));
    }

    return status;
}

NTSTATUS
KbFilter_EvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. Here you can query the device properties
    using WdfFdoInitWdmGetPhysicalDevice/IoGetDeviceProperty and based
    on that, decide to create a filter device object and attach to the
    function stack.

    If you are not interested in filtering this particular instance of the
    device, you can just return STATUS_SUCCESS without creating a framework
    device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    NTSTATUS                status;
    WDFDEVICE               hDevice;
    WDFQUEUE                hQueue;
    PDEVICE_EXTENSION       filterExt;
    WDF_IO_QUEUE_CONFIG     ioQueueConfig;

	WDF_DPC_CONFIG dpcConfig;
	WDF_OBJECT_ATTRIBUTES dpcAttributes;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    DebugPrint(("Enter FilterEvtDeviceAdd \n"));

    //
    // Tell the framework that you are filter driver. Framework
    // takes care of inherting all the device flags & characterstics
    // from the lower device you are attaching to.
    //
    WdfFdoInitSetFilter(DeviceInit);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_KEYBOARD);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_EXTENSION);

    //
    // Create a framework device object.  This call will in turn create
    // a WDM deviceobject, attach to the lower stack and set the
    // appropriate flags and attributes.
    //
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        DebugPrint(("WdfDeviceCreate failed with status code 0x%x\n", status));
        return status;
    }

    filterExt = FilterGetData(hDevice);

	// Give a pointer from the extension struct back to the device
	filterExt->WdfDevice = hDevice;

    //
    // Configure the default queue to be Parallel. Do not use sequential queue
    // if this driver is going to be filtering PS2 ports because it can lead to
    // deadlock. The PS2 port driver sends a request to the top of the stack when it
    // receives an ioctl request and waits for it to be completed. If you use a
    // a sequential queue, this request will be stuck in the queue because of the 
    // outstanding ioctl request sent earlier to the port driver.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    ioQueueConfig.EvtIoInternalDeviceControl = KbFilter_EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            WDF_NO_HANDLE // pointer to default queue
                            );
    if (!NT_SUCCESS(status)) {
        DebugPrint( ("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    //
    // Create a new queue to handle IOCTLs that will be forwarded to us from
    // the rawPDO. 
    //
    WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig,
                             WdfIoQueueDispatchParallel);

    //
    // Framework by default creates non-power managed queues for
    // filter drivers.
    //
    ioQueueConfig.EvtIoDeviceControl = KbFilter_EvtIoDeviceControlFromRawPdo;

    status = WdfIoQueueCreate(hDevice,
                            &ioQueueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES,
                            &hQueue
                            );
    if (!NT_SUCCESS(status)) {
        DebugPrint( ("WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    filterExt->rawPdoQueue = hQueue;

	filterExt->RequestInProgress = NULL;

	//
	// This dpc is fired off when we receive a byte in response to a
	// keyboard write-byte we've done... the dpc routine will execute
	// that completes the request.
	//

	WDF_DPC_CONFIG_INIT(&dpcConfig, KbFiltrCompleteIoctl);

	dpcConfig.AutomaticSerialization = TRUE;

	WDF_OBJECT_ATTRIBUTES_INIT(&dpcAttributes);
	dpcAttributes.ParentObject = filterExt->WdfDevice;

	status = WdfDpcCreate(&dpcConfig,
		&dpcAttributes,
		&filterExt->IoctlCompleteDpc);

	if (!NT_SUCCESS(status))
	{
	        DebugPrint( ("WdfDpcCreate(CommWaitDpc) failed 0x%x\n", status));
		return status;
	}

    //
    // Create a RAW pdo so we can provide a sideband communication with
    // the application. Please note that not filter drivers desire to
    // produce such a communication and not all of them are contrained
    // by other filter above which prevent communication thru the device
    // interface exposed by the main stack. So use this only if absolutely
    // needed. Also look at the toaster filter driver sample for an alternate
    // approach to providing sideband communication.
    //
    status = KbFiltr_CreateRawPdo(hDevice, ++InstanceNo);

    return status;
}

VOID
KbFilter_EvtIoDeviceControlFromRawPdo(
    IN WDFQUEUE      Queue,
    IN WDFREQUEST    Request,
    IN size_t        OutputBufferLength,
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode
    )
/*++

Routine Description:

    This routine is the dispatch routine for device control requests.

Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.

    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.

    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.

Return Value:

   VOID

--*/
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFDEVICE hDevice;
	WDFMEMORY outputMemory;
	PDEVICE_EXTENSION devExt;
	size_t bytesTransferred = 0;
	BYTE *pcRequest =  NULL;
	// BYTE cResponse;

	UNREFERENCED_PARAMETER(InputBufferLength);

	DebugPrint(("Entered KbFilter_EvtIoInternalDeviceControl\n"));

	hDevice = WdfIoQueueGetDevice(Queue);
	devExt = FilterGetData(hDevice);

	//
	// Process the ioctl and complete it when you are done.
	//

	switch (IoControlCode)
	{
	case IOCTL_KBFILTR_GET_KEYBOARD_ATTRIBUTES:
	{
		// This ioctl is from the original kbfiltr DDK sample.

		//
		// Buffer is too small, fail the request
		//

		if (OutputBufferLength < sizeof(KEYBOARD_ATTRIBUTES)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);

		if (!NT_SUCCESS(status))
		{
			DebugPrint(("WdfRequestRetrieveOutputMemory failed %x\n", status));
			break;
		}

		status = WdfMemoryCopyFromBuffer(outputMemory,
			0,
			&devExt->KeyboardAttributes,
			sizeof(KEYBOARD_ATTRIBUTES));

		if (!NT_SUCCESS(status))
		{
			DebugPrint(("WdfMemoryCopyFromBuffer failed %x\n", status));
			break;
		}

		bytesTransferred = sizeof(KEYBOARD_ATTRIBUTES);

		break;    
	}

	case IOCTL_KBFILTR_KEYBOARD_SEND_BYTE:
	{
		/*
		 *	This is our new ioctl for programming the Avant keyboards!  At a high level, it sends a given command byte
		 *	to the keyboard, and gets a response byte back and sends that back to the ioctl caller.  In order to do this,
		 *	we have to disable the i8042's scan code translation while we're getting the keyboard response, because otherwise
		 *	the keyboard response cannot generally be translated back.  Because of all those steps (and because generally we
		 *	can't sit in driver code and wait for the i8042 or keyboard to respond to our commands), this is implemented as a
		 *	state machine which we start here and gets completed one step at a time by our filter driver ISR hook, later in
		 *	this module.  After the last step (when we've received the raw response byte from the keyboard), we reset our
		 *	state machine (so that regular keystroke processing can resume) and send the response byte back to the application
		 *	that issued the ioctl.
		 */

		size_t			length;

		if (InputBufferLength < 1 || OutputBufferLength < 1)
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		// This shouldn't really be needed -- we should only get one ioctl at a time, but doesn't hurt to check...
		if (devExt->RequestState != I8042PGM_NO_REQUEST)
		{
			// TBD: what value to return?  This is an NTSTATUS "warning" - should we use an error (0xC0000000 and above)?
			status = STATUS_DEVICE_BUSY;
			break;
		}

		status = WdfRequestRetrieveInputBuffer(Request,
			1,
			&pcRequest,
			&length);

		if (!NT_SUCCESS(status))
		{
			DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
			break;
		}

		ASSERT(length == InputBufferLength);

		// Save the request byte - it will be sent later
		devExt->cRequest = *pcRequest;

		// Tell the ISR to wait for the command byte confirmation response from DisableScanCodeTranslation().  Note that
		// we do this before sending the disable request below, since the response can come back before that returns!
		devExt->RequestState = I8042PGM_WAIT_CMD_RESPONSE1;

		// Save our request in progress; when the ISR gets the final response, it will queue a DPC to complete our ioctl request...
		devExt->RequestInProgress = Request;

		DebugPrint(("Starting write 0x%X to keyboard.\n", (unsigned) *pcRequest));

		// Start the process by disabling scan code translation in the i8042
		StartDisableScanCodeTranslation(&devExt->cSaveCommandByte);

		DebugPrint(("Started.\n"));

		// Don't complete the request here -- we'll complete it from our ISR's DPC once we receive the response
		return;
	}

	default:
		status = STATUS_NOT_IMPLEMENTED;
		break;
	}
    
	WdfRequestCompleteWithInformation(Request, status, bytesTransferred);

	return;
}

VOID
KbFilter_EvtIoInternalDeviceControl(
    IN WDFQUEUE      Queue,
    IN WDFREQUEST    Request,
    IN size_t        OutputBufferLength,
    IN size_t        InputBufferLength,
    IN ULONG         IoControlCode
    )
/*++

Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:

    IOCTL_INTERNAL_KEYBOARD_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.

    IOCTL_INTERNAL_I8042_HOOK_KEYBOARD:
        Add in the necessary function pointers and context values so that we can
        alter how the ps/2 keyboard is initialized.

    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_KEYBOARD is *NOT* necessary if
           all you want to do is filter KEYBOARD_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and
           functions to conserve space.

Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.

    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.

    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.

Return Value:

   VOID

--*/
{
    PDEVICE_EXTENSION               devExt;
    PINTERNAL_I8042_HOOK_KEYBOARD   hookKeyboard = NULL;
    PCONNECT_DATA                   connectData = NULL;
    NTSTATUS                        status = STATUS_SUCCESS;
    size_t                          length;
    WDFDEVICE                       hDevice;
    BOOLEAN                         forwardWithCompletionRoutine = FALSE;
    BOOLEAN                         ret = TRUE;
    WDFCONTEXT                      completionContext = WDF_NO_CONTEXT;
    WDF_REQUEST_SEND_OPTIONS        options;
    WDFMEMORY                       outputMemory;
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);


    PAGED_CODE();

    DebugPrint(("Entered KbFilter_EvtIoInternalDeviceControl\n"));

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch (IoControlCode) {

    //
    // Connect a keyboard class device driver to the port driver.
    //
    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        //
        // Only allow one connection.
        //
        if (devExt->UpperConnectData.ClassService != NULL) {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        //
        // Get the input buffer from the request
        // (Parameters.DeviceIoControl.Type3InputBuffer).
        //
        status = WdfRequestRetrieveInputBuffer(Request,
                                    sizeof(CONNECT_DATA),
                                    &connectData,
                                    &length);
        if(!NT_SUCCESS(status)){
            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }

        ASSERT(length == InputBufferLength);

        devExt->UpperConnectData = *connectData;

        //
        // Hook into the report chain.  Everytime a keyboard packet is reported
        // to the system, KbFilter_ServiceCallback will be called
        //

        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);

#pragma warning(disable:4152)  //nonstandard extension, function/data pointer conversion

        connectData->ClassService = KbFilter_ServiceCallback;

#pragma warning(default:4152)

        break;

    //
    // Disconnect a keyboard class device driver from the port driver.
    //
    case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:

        //
        // Clear the connection parameters in the device extension.
        //
        // devExt->UpperConnectData.ClassDeviceObject = NULL;
        // devExt->UpperConnectData.ClassService = NULL;

        status = STATUS_NOT_IMPLEMENTED;
        break;

    //
    // Attach this driver to the initialization and byte processing of the
    // i8042 (ie PS/2) keyboard.  This is only necessary if you want to do PS/2
    // specific functions, otherwise hooking the CONNECT_DATA is sufficient
    //
    case IOCTL_INTERNAL_I8042_HOOK_KEYBOARD:

        DebugPrint(("hook keyboard received!\n"));

        //
        // Get the input buffer from the request
        // (Parameters.DeviceIoControl.Type3InputBuffer)
        //
        status = WdfRequestRetrieveInputBuffer(Request,
                            sizeof(INTERNAL_I8042_HOOK_KEYBOARD),
                            &hookKeyboard,
                            &length);
        if(!NT_SUCCESS(status)){
            DebugPrint(("WdfRequestRetrieveInputBuffer failed %x\n", status));
            break;
        }

        ASSERT(length == InputBufferLength);

        //
        // Enter our own initialization routine and record any Init routine
        // that may be above us.  Repeat for the isr hook
        //
        devExt->UpperContext = hookKeyboard->Context;

        //
        // replace old Context with our own
        //
        hookKeyboard->Context = (PVOID) devExt;

        if (hookKeyboard->InitializationRoutine) {
            devExt->UpperInitializationRoutine =
                hookKeyboard->InitializationRoutine;
        }
        hookKeyboard->InitializationRoutine =
            (PI8042_KEYBOARD_INITIALIZATION_ROUTINE)
            KbFilter_InitializationRoutine;

        if (hookKeyboard->IsrRoutine) {
            devExt->UpperIsrHook = hookKeyboard->IsrRoutine;
        }
        hookKeyboard->IsrRoutine = (PI8042_KEYBOARD_ISR) KbFilter_IsrHook;

        //
        // Store all of the other important stuff
        //
        devExt->IsrWritePort = hookKeyboard->IsrWritePort;
        devExt->QueueKeyboardPacket = hookKeyboard->QueueKeyboardPacket;
        devExt->CallContext = hookKeyboard->CallContext;

        status = STATUS_SUCCESS;
        break;


    case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
        forwardWithCompletionRoutine = TRUE;
        completionContext = devExt;
        break;
        
    //
    // Might want to capture these in the future.  For now, then pass them down
    // the stack.  These queries must be successful for the RIT to communicate
    // with the keyboard.
    //
    case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
    case IOCTL_KEYBOARD_QUERY_INDICATORS:
    case IOCTL_KEYBOARD_SET_INDICATORS:
    case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
    case IOCTL_KEYBOARD_SET_TYPEMATIC:
        break;
    }

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    //
    // Forward the request down. WdfDeviceGetIoTarget returns
    // the default target, which represents the device attached to us below in
    // the stack.
    //

    if (forwardWithCompletionRoutine) {

        //
        // Format the request with the output memory so the completion routine
        // can access the return data in order to cache it into the context area
        //
        
        status = WdfRequestRetrieveOutputMemory(Request, &outputMemory); 

        if (!NT_SUCCESS(status)) {
            DebugPrint(("WdfRequestRetrieveOutputMemory failed: 0x%x\n", status));
            WdfRequestComplete(Request, status);
            return;
        }

        status = WdfIoTargetFormatRequestForInternalIoctl(WdfDeviceGetIoTarget(hDevice),
                                                         Request,
                                                         IoControlCode,
                                                         NULL,
                                                         NULL,
                                                         outputMemory,
                                                         NULL);

        if (!NT_SUCCESS(status)) {
            DebugPrint(("WdfIoTargetFormatRequestForInternalIoctl failed: 0x%x\n", status));
            WdfRequestComplete(Request, status);
            return;
        }
    
        // 
        // Set our completion routine with a context area that we will save
        // the output data into
        //
        WdfRequestSetCompletionRoutine(Request,
                                    KbFilterRequestCompletionRoutine,
                                    completionContext);

        ret = WdfRequestSend(Request,
                             WdfDeviceGetIoTarget(hDevice),
                             WDF_NO_SEND_OPTIONS);

        if (ret == FALSE) {
            status = WdfRequestGetStatus (Request);
            DebugPrint( ("WdfRequestSend failed: 0x%x\n", status));
            WdfRequestComplete(Request, status);
        }

    }
    else
    {

        //
        // We are not interested in post processing the IRP so 
        // fire and forget.
        //
        WDF_REQUEST_SEND_OPTIONS_INIT(&options,
                                      WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

        ret = WdfRequestSend(Request, WdfDeviceGetIoTarget(hDevice), &options);

        if (ret == FALSE) {
            status = WdfRequestGetStatus (Request);
            DebugPrint(("WdfRequestSend failed: 0x%x\n", status));
            WdfRequestComplete(Request, status);
        }
        
    }

    return;
}

NTSTATUS
KbFilter_InitializationRoutine(
    IN PVOID                           InitializationContext,
    IN PVOID                           SynchFuncContext,
    IN PI8042_SYNCH_READ_PORT          ReadPort,
    IN PI8042_SYNCH_WRITE_PORT         WritePort,
    OUT PBOOLEAN                       TurnTranslationOn
    )
/*++

Routine Description:

    This routine gets called after the following has been performed on the kb
    1)  a reset
    2)  set the typematic
    3)  set the LEDs

    i8042prt specific code, if you are writing a packet only filter driver, you
    can remove this function

Arguments:

    DeviceObject - Context passed during IOCTL_INTERNAL_I8042_HOOK_KEYBOARD

    SynchFuncContext - Context to pass when calling Read/WritePort

    Read/WritePort - Functions to synchronoulsy read and write to the kb

    TurnTranslationOn - If TRUE when this function returns, i8042prt will not
                        turn on translation on the keyboard

Return Value:

    Status is returned.

--*/
{
    PDEVICE_EXTENSION  devExt;
    NTSTATUS            status = STATUS_SUCCESS;

    devExt = (PDEVICE_EXTENSION)InitializationContext;

    //
    // Do any interesting processing here.  We just call any other drivers
    // in the chain if they exist.  Make sure Translation is turned on as well
    //
    if (devExt->UpperInitializationRoutine) {
        status = (*devExt->UpperInitializationRoutine) (
                        devExt->UpperContext,
                        SynchFuncContext,
                        ReadPort,
                        WritePort,
                        TurnTranslationOn
                        );

        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    // This was left as-is from Microsoft's kbfiltr sample.  Note that we have to disable scan translation when
    // we send each byte and receive the response from the Avant keyboard, because we need to send and receive
    // all byte values 00-FF without any translation.  We disable and reenable scan translation ourselves, because
    // the i8042prt driver doesn't provide any callbacks to do it, and we can't do it here or else translation will
    // stay off for the lifetime of the keyboard device, which means the keyboard won't work :)

    *TurnTranslationOn = TRUE;
    return status;
}

BOOLEAN
KbFilter_IsrHook(
    PVOID                  IsrContext,
    PKEYBOARD_INPUT_DATA   CurrentInput,
    POUTPUT_PACKET         CurrentOutput,
    UCHAR                  StatusByte,
    PUCHAR                 DataByte,
    PBOOLEAN               ContinueProcessing,
    PKEYBOARD_SCAN_STATE   ScanState
    )
/*++

Routine Description:

    This routine gets called at the beginning of processing of the kb interrupt.

    i8042prt specific code, if you are writing a packet only filter driver, you
    can remove this function

Arguments:

    DeviceObject - Our context passed during IOCTL_INTERNAL_I8042_HOOK_KEYBOARD

    CurrentInput - Current input packet being formulated by processing all the
                    interrupts

    CurrentOutput - Current list of bytes being written to the keyboard or the
                    i8042 port.

    StatusByte    - Byte read from I/O port 60 when the interrupt occurred

    DataByte      - Byte read from I/O port 64 when the interrupt occurred.
                    This value can be modified and i8042prt will use this value
                    if ContinueProcessing is TRUE

    ContinueProcessing - If TRUE, i8042prt will proceed with normal processing of
                         the interrupt.  If FALSE, i8042prt will return from the
                         interrupt after this function returns.  Also, if FALSE,
                         it is this functions responsibility to report the input
                         packet via the function provided in the hook IOCTL or via
                         queueing a DPC within this driver and calling the
                         service callback function acquired from the connect IOCTL

Return Value:

    Status is returned.

--*/
{
	PDEVICE_EXTENSION devExt;
	BOOLEAN           retVal = TRUE;

	devExt = (PDEVICE_EXTENSION)IsrContext;

	// See if the received byte is a response to a request we sent for Avant programming...
	switch (devExt->RequestState)
	{
		case I8042PGM_NO_REQUEST:
			// Not our request -- do the normal ISR work
			break;

		case I8042PGM_WAIT_CMD_RESPONSE1:
			// We are about to disable scan translation and are receiving the current i8042 command byte
			devExt->cSaveCommandByte = *DataByte;

			// Make a copy of the command byte value, which we'll modify below
			devExt->cOurCommandByte = devExt->cSaveCommandByte;

			DebugPrint(("Got initial cmd byte: 0x%X!\n", (unsigned) devExt->cSaveCommandByte));

			// Now modify the command byte to turn off scan code translation in the i8042
			if (!DisableScanCodeTranslationStep3(&devExt->cOurCommandByte))
			{
				// TBD: error - set RequestState to NO_REQUEST, complete the ioctl with an error
			}

			// Now the i8042 will send back the updated command byte so we can verify that our change worked...
			devExt->RequestState = I8042PGM_WAIT_CMD_RESPONSE2;

			// Don't process the input byte any further -- we eat it and pretend it never happened!
			*ContinueProcessing = FALSE;

			return TRUE;

		case I8042PGM_WAIT_CMD_RESPONSE2:
			// We received the command byte response from the end of DisableScanCodeTranslationStep3()
			if (*DataByte != devExt->cOurCommandByte)
			{
				// TBD: error - set RequestState to NO_REQUEST, complete the ioctl with an error
			}

			DebugPrint(("Got verify cmd byte: 0x%X!\n", (unsigned) *DataByte));

			/*
			 *	Command byte is set... now send the ioctl's request byte for programming the Avant keyboard.
			 *	We could maybe instead do this using our own kbif.c routines, but this seems to work well enough.
			 *	The i8042prt driver's IsrWritePort callback does *not* wait for an ack from the keyboard, which
			 *	is important, since we have disabled scan code translation and the Avant keyboard's response
			 *	byte might confuse the stock i8042prt driver...
			 */

			(*devExt->IsrWritePort)(devExt->CallContext, devExt->cRequest);

			// Enter wait for response byte
			devExt->RequestState = I8042PGM_WAIT_BYTE_RESPONSE;

			// Don't process the byte further -- we eat it and pretend it never happened!
			*ContinueProcessing = FALSE;

			return TRUE;

		case I8042PGM_WAIT_BYTE_RESPONSE:
			// Now we have received the response byte (untranslated!) from the Avant keyboard; save it off...
			devExt->cResponse = *DataByte;

			DebugPrint(("Got response: 0x%X!\n", (unsigned) *DataByte));

			// Now restore the i8042's command byte, to turn scan translation back on
			SetCommandBytePart1(devExt->cSaveCommandByte);

			// Enter wait for command byte verification
			devExt->RequestState = I8042PGM_WAIT_CMD_RESPONSE3;

			// Don't process the byte further -- we eat it and pretend it never happened!
			*ContinueProcessing = FALSE;

			return TRUE;

		case I8042PGM_WAIT_CMD_RESPONSE3:
			// Got the restored command byte back
			if (*DataByte != devExt->cSaveCommandByte)
			{
				// TBD: error - set RequestState to NO_REQUEST, complete the ioctl with an error
			}

			DebugPrint(("Got verify old cmd byte: 0x%X!\n", (unsigned) *DataByte));

			// Now we're all done with the hardware... the last thing to do is complete the user's ioctl.
			// We fire off a DPC which will return the keyboard response byte to the ioctl caller.
			retVal = WdfDpcEnqueue(devExt->IoctlCompleteDpc);

			if (retVal)
			{
				// Don't process the response byte further -- we eat it and pretend it never happened!
				*ContinueProcessing = FALSE;

				return TRUE;
			}

			// TBD: error handling
			break;

		default:
			// TBD: invalid state?!  set RequestState to NO_REQUEST, complete the ioctl with an error
			break;

	} // switch(devExt->RequestState)

	DebugPrint(("ISR got byte: 0x%X!\n", (unsigned) *DataByte));

	if (devExt->UpperIsrHook)
	{
		retVal = (*devExt->UpperIsrHook) (
			devExt->UpperContext,
			CurrentInput,
			CurrentOutput,
			StatusByte,
			DataByte,
			ContinueProcessing,
			ScanState
			);

		if (!retVal || !(*ContinueProcessing))
		{
			return retVal;
		}
	}

	// Normal processing: hand the received byte to the next driver, for normal keyboard processing
	*ContinueProcessing = TRUE;
	return retVal;
}

VOID
KbFilter_ServiceCallback(
    IN PDEVICE_OBJECT  DeviceObject,
    IN PKEYBOARD_INPUT_DATA InputDataStart,
    IN PKEYBOARD_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    )
/*++

Routine Description:

    Called when there are keyboard packets to report to the Win32 subsystem.
    You can do anything you like to the packets.  For instance:

    o Drop a packet altogether
    o Mutate the contents of a packet
    o Insert packets into the stream

Arguments:

    DeviceObject - Context passed during the connect IOCTL

    InputDataStart - First packet to be reported

    InputDataEnd - One past the last packet to be reported.  Total number of
                   packets is equal to InputDataEnd - InputDataStart

    InputDataConsumed - Set to the total number of packets consumed by the RIT
                        (via the function pointer we replaced in the connect
                        IOCTL)

Return Value:

    Status is returned.

--*/
{
    PDEVICE_EXTENSION   devExt;
    WDFDEVICE   hDevice;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);

    devExt = FilterGetData(hDevice);

    (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR) devExt->UpperConnectData.ClassService)(
        devExt->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed);
}

VOID
KbFilterRequestCompletionRoutine(
    WDFREQUEST                  Request,
    WDFIOTARGET                 Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT                  Context
   )
/*++

Routine Description:

    Completion Routine

Arguments:

    Target - Target handle
    Request - Request handle
    Params - request completion params
    Context - Driver supplied context


Return Value:

    VOID

--*/
{
    WDFMEMORY buffer = CompletionParams->Parameters.Ioctl.Output.Buffer;

    UNREFERENCED_PARAMETER(Target);
    //
    // Save the keyboard attributes in our context area so that we can return
    // them to the app later.
    //
    if (CompletionParams->Type == WdfRequestTypeDeviceControlInternal &&
        NT_SUCCESS(CompletionParams->IoStatus.Status) && 
        CompletionParams->Parameters.Ioctl.IoControlCode == IOCTL_KEYBOARD_QUERY_ATTRIBUTES) {

        if( CompletionParams->Parameters.Ioctl.Output.Length >= sizeof(KEYBOARD_ATTRIBUTES)) {
            WdfMemoryCopyToBuffer(buffer,
                                  CompletionParams->Parameters.Ioctl.Output.Offset,
                                  &((PDEVICE_EXTENSION)Context)->KeyboardAttributes,
                                  sizeof(KEYBOARD_ATTRIBUTES)
                                  );
        }
    }

    WdfRequestComplete(Request, CompletionParams->IoStatus.Status);

    return;
}

//
// DPC routine for completing the Avant keyboard send-byte response
//

VOID KbFiltrCompleteIoctl(IN WDFDPC Dpc)
{
	PDEVICE_EXTENSION devExt;
	WDFREQUEST Request;
	NTSTATUS status;
	WDFMEMORY outputMemory;
	size_t bytesTransferred = 0;

	DebugPrint(("KbFiltrCompleteIoctl in.\n"));

	devExt = FilterGetData(WdfDpcGetParentObject(Dpc));

	Request = devExt->RequestInProgress;

	if (Request == NULL)
	{
		DebugPrint(("Got DPC with no pending request!!\n"));
		return;
	}

	status = WdfRequestRetrieveOutputMemory(Request, &outputMemory);

	if (!NT_SUCCESS(status))
	{
		DebugPrint(("WdfRequestRetrieveOutputMemory failed %x\n", status));
		return;
	}

	status = WdfMemoryCopyFromBuffer(outputMemory,
		0,
		&devExt->cResponse,
		1);

	if (!NT_SUCCESS(status))
	{
		DebugPrint(("WdfMemoryCopyFromBuffer failed %x\n", status));
		return;
	}

	bytesTransferred = 1;

	WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, bytesTransferred);

	devExt->RequestState = I8042PGM_NO_REQUEST;
	devExt->RequestInProgress = NULL;

	DebugPrint(("KbFiltrCompleteIoctl out byte: 0x%X.\n", (unsigned) devExt->cResponse));
}

