/*
 *	kbif.c
 *
 *	Low-level keyboard interface routines for the i8042-based keyboard programming driver.
 *
 *	This stuff is loosely based on code, examples and documentation from several sources:
 *
 *	- The old Avant DOS software (avantdwn.exe)
 *	- The Avant Win9x software (ifvkd.vxd)
 *	- Example programs from The Undocumented PC's keyboard chapter
 *	- The Win2K DDK i8042prt driver source code
 *
 *	However, note that the above old code would operate step by step in a single stream
 *	of operations with interrupts disabled.  In a Windows driver we cannot do that -- we
 *	have to perform a limited number of I/O operations and then return back to the kernel.
 *	We cannot sit in a loop and wait for a response to come back from the i8042 or the
 *	keyboard; the keyboard port driver's ISR must handle that, and will notify us via our
 *	ISR hook.
 */

#include <ntddk.h>		// various NT definitions
#include <ntintsafe.h>	// BYTE etc. for drivers
#include <string.h>

#include "kbif.h"		// Just has our function decls

#if DBG	// Checked build

	#define TRAP()						DbgBreakPoint()

	#define DebugPrint(_x_)				DbgPrint _x_

#else   // Free build

	#define TRAP()

	#define DebugPrint(_x_)

#endif

// This stuff is from the old Win2K DDK version of i8042prt.sys
#define I8042_READ_CONTROLLER_COMMAND_BYTE	0x20
#define I8042_WRITE_CONTROLLER_COMMAND_BYTE	0x60

// Command byte flags we modify
#define CCB_KEYBOARD_TRANSLATE_MODE		0x40


// Define the 8042 Controller Status Register bits.
#define OUTPUT_BUFFER_FULL			0x01
#define INPUT_BUFFER_FULL			0x02


unsigned uI8042CommandPort = 0x64;
unsigned uI8042DataPort = 0x60;



/*
 *	IODelay
 *
 *	Currently a no-op, except for the call/return sequence itself.  I'm not sure if this is even
 *	needed -- it was in the original DOS code (avantdwn.exe) and in the Windows 9x VxD code (ifvkd.vxd),
 *	but it might have only been needed on some really old or buggy i8042 chips, and/or on really old CPUs?
 *	There's nothing like this in the Win2K DDK i8042prt.sys code, and it didn't seem to be needed in my testing.
 */

void IODelay()
{
}

BYTE GetStatusByte()
{
	BYTE cVal = READ_PORT_UCHAR((PUCHAR) uI8042CommandPort);

	IODelay();

	return cVal;
}

void PutCommandByte(BYTE cVal)
{
	WRITE_PORT_UCHAR((PUCHAR) uI8042CommandPort, cVal);

	IODelay();

	DebugPrint(("Wrote command: 0x%X.\n", cVal));
}

BYTE GetDataByte()
{
	BYTE cVal = READ_PORT_UCHAR((PUCHAR) uI8042DataPort);

	IODelay();

	return cVal;
}

void PutDataByte(BYTE cVal)
{
	WRITE_PORT_UCHAR((PUCHAR) uI8042DataPort, cVal);

	IODelay();

	DebugPrint(("Wrote byte: 0x%X.\n", cVal));
}

#if 0

void EatReadData()
{
	BYTE cStatus, cData;

	for (;;)
	{
		cStatus = GetStatusByte();

		if ((cStatus & 1) == 0)
			break;

		cData = GetDataByte();

		DebugPrint(("Ate byte: 0x%X.\n", cData));
	}
}

#endif

int WaitForWriteDataReady()
{
	BYTE cStatus;
	int nLoop;

	for (nLoop = 0; nLoop < 65536; ++nLoop)
	{
		cStatus = GetStatusByte();

		if ((cStatus & 2) == 0)
		{
			DebugPrint(("Wait-write-ready: %d loops.\n", nLoop));
			return TRUE;
		}
	}

	DebugPrint(("Wait-write-ready failed!\n"));

	return FALSE;
}

int FinishSend()
{
	return WaitForWriteDataReady();
}

int StartDisableScanCodeTranslation()
{
	// TBD: keep this in? Let ISR handle it?  This was in ifvkd.vxd, but we're in a different world now :)
	// EatReadData();

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to get write ready before sending get-command-byte request!\n"));
		return FALSE;
	}

	PutCommandByte(I8042_READ_CONTROLLER_COMMAND_BYTE);

	if (!FinishSend())
	{
		DebugPrint(("Failed to finish send to get-command-byte request!\n"));
		return FALSE;
	}

	// The ISR will handle the response when it comes back...
	return TRUE;
}

int DisableScanCodeTranslationStep3(BYTE *pcCommandByte)
{
	BYTE cNewCommandByte;

	/*
	 *	This code is slightly different from what the old DOS avantdwn.exe and the Win9x ifvkd.vxd do --
	 *	Those set the command byte to 0, thus disabling scan translation as well as keyboard interrupts.
	 *	Here, we can't disable interrupts in the i8042 -- if we try, things will act flaky, presumably
	 *	because the ISR still gets called if some synchronous operation detects a read byte is present
	 *	in the i8042.  Also, both the DOS code and VxD did a CLI / STI around the port I/O code, which
	 *	we also cannot do on modern Windows, because we'll only disable interrupts for one core, but
	 *	keyboard interrupts will still come in on other cores.
	 *
	 *	The correct way is to let the ISR handle responses from the i8042 (whether from the i8042 itself
	 *	or for bytes received from the keyboard), and our ISR hook either processes them (if it's a
	 *	response to something we instigated) or forwards them on for regular processing.
	 *
	 *	So what we do is read the current command byte (which we did earlier and which is passed to
	 *	us here as *pcCommandByte), turn off the scan-translation bit, and write the command byte.
	 */

	cNewCommandByte = *pcCommandByte & (~CCB_KEYBOARD_TRANSLATE_MODE);

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to get write ready after sending get-command-byte request!\n"));
		return FALSE;
	}

	PutCommandByte(I8042_WRITE_CONTROLLER_COMMAND_BYTE);

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to get write ready after sending set-command-byte request!\n"));
		return FALSE;
	}

	PutDataByte(cNewCommandByte);	// TBD: define? just mask out the translation bit?

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to wait for write ready after set-command-byte value!\n"));
		return FALSE;
	}

	// Now read back the command byte to make sure it 'stuck'
	PutCommandByte(I8042_READ_CONTROLLER_COMMAND_BYTE);

	if (!FinishSend())
	{
		DebugPrint(("Failed to finish send to get-new-command-byte value!\n"));
		return FALSE;
	}

	// Return the modified command byte... the ISR will wait for the confirmation response
	*pcCommandByte = cNewCommandByte;

	return TRUE;
}

int SetCommandBytePart1(BYTE cCommandByte)
{
	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to get write ready before sending command byte request!\n"));
		return FALSE;
	}

	PutCommandByte(I8042_WRITE_CONTROLLER_COMMAND_BYTE);

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to get write ready after sending command byte request!\n"));
		return FALSE;
	}

	PutDataByte(cCommandByte);

	if (!WaitForWriteDataReady())
	{
		DebugPrint(("Failed to wait for write ready after sending command byte value!\n"));
		return FALSE;
	}

	DebugPrint(("Put command byte back to: %x.\n", (unsigned) cCommandByte));

	// this shouldn't be needed, but let's try it...
	PutCommandByte(I8042_READ_CONTROLLER_COMMAND_BYTE);

	if (!FinishSend())
	{
		DebugPrint(("Failed to finish send of get-new-command-byte value!\n"));
		return FALSE;
	}

	// The ISR will handle the response when it comes back...
	return TRUE;
}

