/*++

The original ioctl code came from the Win7 DDK's src\input\kbfiltr\exe sample,
which carried this copyright and info:

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Abstract:


Environment:

    usermode console application

--*/


#define _CRT_SECURE_NO_WARNINGS

#include <basetyps.h>
#include <stdlib.h>
#include <wtypes.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <ntddkbd.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#pragma warning(disable:4201)

#include <setupapi.h>
#include <winioctl.h>

#pragma warning(default:4201)

#include "..\AvantFlt\public.h"
#include "KbdPgm.h"


//-----------------------------------------------------------------------------
// 4127 -- Conditional Expression is Constant warning
//-----------------------------------------------------------------------------
#define WHILE(constant) \
__pragma(warning(disable: 4127)) while(constant); __pragma(warning(default: 4127))

BYTE gcBytesToSend[4096];
int gnBytesToSend;

BYTE gcKBDFile[4096];

// Detected keyboard type -- See KBD_ID_xxx in kbdpgm.h
unsigned guKeyboardType;

// The currently-loaded keyboard program -- whether it was read from a file or from
// the keyboard itself, it goes into this structure!
KeyboardProgram gProgram;

// TRUE if gProgram has been read from the keyboard or from a file
BOOL gbGotProgram;

// Simulation mode generates the progamming bytes but doesn't actually send anything to the keyboard,
// to avoid hosing the keyboard programming during development :)
BOOL gbSimulationMode;

// Simulation mode is only enabled during actual keyboard programming
BOOL gbEnableSimulation;

// Function forward decls
int RealMain(HANDLE hFile, int argc, char *argv[]);
BOOL ReadKeyboardProgram(HANDLE hDriver);
BOOL WriteProgramToFile(LPCSTR pszFile);
BOOL ReadProgramFromFile(LPCSTR pszFile);
BOOL WriteProgramToKeyboard(HANDLE hDriver);
void PrintProgram(BOOL bVerbose);


unsigned int SendKeyboard(HANDLE file, BYTE cRequest)
{
	BYTE	cResponse;
	ULONG	bytes = 0;

	if (gbSimulationMode && gbEnableSimulation)
	{
		printf("Send byte to keyboard: 0x%X\n", cRequest);

		// Sim-mode only works for keyboard programming requests which return 0xFA!
		return KBD_CMD_ACK;
	}

	if (!DeviceIoControl (file,
		IOCTL_KBFILTR_KEYBOARD_SEND_BYTE,
		&cRequest, 1,
		&cResponse, 1,
		&bytes, NULL))
	{
		return (unsigned) -1;
	}
	else
		return cResponse;
}

void GetReady(LPCSTR pszForWhat)
{
	printf("Hit Enter to %s:", pszForWhat);
	getchar();

	printf("Waiting...");
	Sleep(1000);
	printf("here we go!\n");
}

void SendBytesToKeyboard(HANDLE hFile)
{
	BYTE			cRequest;
	unsigned		uResponse;
	int				nLoop;

	GetReady("send keyboard bytes");

	for (nLoop = 0; nLoop < gnBytesToSend; ++nLoop)
	{
		cRequest = gcBytesToSend[nLoop];

		if ((uResponse = SendKeyboard(hFile, cRequest)) == (unsigned)-1)
		{
			printf("Keyboard send error: 0x%x\n", GetLastError());
		}
		else
		{
			printf("Send(0x%x) succeeded; response = 0x%X\n", cRequest, uResponse);
		}

		// No delay seems to be needed between bytes sent to the keyboard, but add this for testing/debugging if needed
		// Sleep(1000);
	}
}

int ShowUsage()
{
	printf(
		"The following command-line functions are standalone and may not be combined with other options:\n"
		"    avantcmd -send byte1 byte2 ...\n"
		"        Sends specified bytes to keyboard and prints the response byte for each one.\n"
		"        Specify bytes in hex with no 0x prefix or h suffix.  For example:\n"
		"        avantcmd -send EE EB\n"
		"    avantcmd -sendfile file.ext\n"
		"        Reads bytes from specified file and sends them to the keyboard, printing the response\n"
		"        byte for each.\n"
		"    avantcmd -flash\n"
		"        Flashes the keyboard lights on and off once.\n"
		"    avantcmd -id\n"
		"        Sends the Avant keyboard-ID command and prints the response.\n"
		"\n"
		"The following functions may be combined on the command line:\n"
		"    -readkb\n"
		"        Reads the current programming from the keyboard.\n"
		"    -readfile file.kbd\n"
		"        Reads programming data from the specified .kbd file.\n"
		"    -verbose\n"
		"        Use before -print to print all key mappings, including standard ones.\n"
		"    -print\n"
		"        Prints the currently-loaded programming data to stdout.\n"
		"    -simulate\n"
		"        Does not actually send programming commands to keyboard; for testing.\n"
		"    -writekb\n"
		"        Writes the currently-loaded programming data to the keyboard.\n"
		"    -writefile file.kbd\n"
		"        Writes the currently-loaded programming data to the specified .kbd file.\n"
	);

	return -1;
}

HANDLE OpenKeyboardFilter()
{
	HDEVINFO							hardwareDeviceInfo;
	SP_DEVICE_INTERFACE_DATA			deviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	deviceInterfaceDetailData = NULL;
	ULONG			predictedLength = 0;
	ULONG			requiredLength = 0;
	HANDLE			file;
	ULONG			i = 0;

	//
	// Open a handle to our i8042 keyboard programming filter driver (actually the raw PDO)
	//

	hardwareDeviceInfo = SetupDiGetClassDevs(
		(LPGUID)&GUID_DEVINTERFACE_KBFILTER,
		NULL, // Define no enumerator (global)
		NULL, // Define no
		(DIGCF_PRESENT | // Only Devices present
			DIGCF_DEVICEINTERFACE)); // Function class devices.

	if (INVALID_HANDLE_VALUE == hardwareDeviceInfo)
	{
		printf("SetupDiGetClassDevs failed: %x\n", GetLastError());
		return 0;
	}

	deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	printf("\nList of AvantFlt Device Interfaces\n");
	printf("---------------------------------\n");

	i = 0;

	//
	// Enumerate devices of toaster class
	//

	do
	{
		if (SetupDiEnumDeviceInterfaces(hardwareDeviceInfo,
			0, // No care about specific PDOs
			(LPGUID)&GUID_DEVINTERFACE_KBFILTER,
			i, //
			&deviceInterfaceData))
		{
			if (deviceInterfaceDetailData)
			{
				free(deviceInterfaceDetailData);
				deviceInterfaceDetailData = NULL;
			}

			//
			// Allocate a function class device data structure to
			// receive the information about this particular device.
			//

			//
			// First find out required length of the buffer
			//

			if (!SetupDiGetDeviceInterfaceDetail(
				hardwareDeviceInfo,
				&deviceInterfaceData,
				NULL, // probing so no output buffer yet
				0, // probing so output buffer length of zero
				&requiredLength,
				NULL))
			{
				// not interested in the specific dev-node
				if (ERROR_INSUFFICIENT_BUFFER != GetLastError())
				{
					printf("SetupDiGetDeviceInterfaceDetail failed %d\n", GetLastError());
					SetupDiDestroyDeviceInfoList(hardwareDeviceInfo);
					return FALSE;
				}
			}

			predictedLength = requiredLength;

			deviceInterfaceDetailData = malloc(predictedLength);

			if (deviceInterfaceDetailData)
			{
				deviceInterfaceDetailData->cbSize =
					sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			}
			else
			{
				printf("Couldn't allocate %d bytes for device interface details.\n", predictedLength);
				SetupDiDestroyDeviceInfoList(hardwareDeviceInfo);
				return FALSE;
			}

			if (!SetupDiGetDeviceInterfaceDetail(
				hardwareDeviceInfo,
				&deviceInterfaceData,
				deviceInterfaceDetailData,
				predictedLength,
				&requiredLength,
				NULL))
			{
				printf("Error in SetupDiGetDeviceInterfaceDetail\n");
				SetupDiDestroyDeviceInfoList(hardwareDeviceInfo);
				free(deviceInterfaceDetailData);
				return FALSE;
			}

			printf("%d) %s\n", ++i,
				deviceInterfaceDetailData->DevicePath);
		}
		else if (ERROR_NO_MORE_ITEMS != GetLastError())
		{
			free(deviceInterfaceDetailData);
			deviceInterfaceDetailData = NULL;
			continue;
		}
		else
			break;

	} WHILE(TRUE);

	SetupDiDestroyDeviceInfoList(hardwareDeviceInfo);

	if (!deviceInterfaceDetailData)
	{
		printf("No AvantFlt keyboard programming device interfaces were found.\n");
		return 0;
	}

	//
	// Open the last device interface
	//

	printf("\nOpening the last interface:\n %s\n\n",
		deviceInterfaceDetailData->DevicePath);

	file = CreateFile(deviceInterfaceDetailData->DevicePath,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL, // no SECURITY_ATTRIBUTES structure
		OPEN_EXISTING, // No special create flags
		0, // No special attributes
		NULL);

	free(deviceInterfaceDetailData);

	if (INVALID_HANDLE_VALUE == file)
		printf("Error %x opening device path '%s'!", GetLastError(), deviceInterfaceDetailData->DevicePath);

	return file;
}

int ReadFileIntoBuffer(LPCSTR pszFile, BYTE *pcBuffer, unsigned uMaxBuf)
{
	int nBytes;

	int fd = _open(pszFile, O_BINARY | O_RDONLY);

	if (fd == -1)
	{
		printf("Error %d opening file '%s'!\n", errno, pszFile);
		return -1;
	}

	if ((nBytes = _read(fd, pcBuffer, uMaxBuf)) == -1)
	{
		printf("Error %d reading file '%s'!\n", errno, pszFile);
		_close(fd);
		return -1;
	}
	else if ((unsigned) nBytes >= uMaxBuf)
	{
		printf("File '%s' is too big (max %u chars).\n", pszFile, uMaxBuf);
		_close(fd);
		return -1;
	}

	// else we read the data
	_close(fd);

	return nBytes;
}

int _cdecl main(__in int argc, __in char *argv[])
{
	HANDLE			hDriver;
	int				nRetVal;

	if (argc < 2)
	{
		return ShowUsage();
	}

	hDriver = OpenKeyboardFilter();

	if (INVALID_HANDLE_VALUE == hDriver)
		return;

	nRetVal = RealMain(hDriver, argc, argv);

	CloseHandle(hDriver);
	return nRetVal;
}

int RealMain(HANDLE hDriver, int argc, char *argv[])
{
	unsigned		uResponse;
	unsigned		uByte;
	char			*pszArg;
	BOOL			bVerbose = FALSE;

	++argv;
	--argc;

	while (argc)
	{
		pszArg = *argv;

		if (pszArg[0] != '-' && pszArg[0] != '/')
		{
			// All arguments start with a - or /
			printf("Invalid parameter: <%s>\n", pszArg);

			return ShowUsage();
		}

		++pszArg;

		if (!strcmp(pszArg, "?") || !strcmp(pszArg, "h") || !strcmp(pszArg, "help"))
		{
			return ShowUsage();
		}

		if (!strcmp(pszArg, "send"))
		{
			// The remaining args are taken as bytes (can't have other options after the list of bytes)
			while (--argc)
			{
				if (sscanf(*++argv, "%x", &uByte) != 1)
				{
					printf("Invalid byte value: %s\n", *argv);
					return -1;
				}

				gcBytesToSend[gnBytesToSend++] = (BYTE)uByte;
			}

			SendBytesToKeyboard(hDriver);
			return 0;
		}

		if (!strcmp(pszArg, "sendfile"))
		{
			if (argc <= 1)
			{
				printf("Please supply the filename containing bytes to send to the keyboard.\n");
				return -1;
			}

			if ((gnBytesToSend = ReadFileIntoBuffer(*++argv, gcBytesToSend, sizeof(gcBytesToSend))) == -1)
				return -1;

			SendBytesToKeyboard(hDriver);
			return 0;
		}

		// All the args below take zero or one parameters, and may be followed by other args
		if (!strcmp(pszArg, "flash"))
		{
			// Flash the keyboard LEDs once, just for giggles
			printf("Flashing LEDs...\n");

			uResponse = SendKeyboard(hDriver, 0xED);
			printf("First response = 0x%X\n", uResponse);
			uResponse = SendKeyboard(hDriver, 0x07);
			printf("Second response = 0x%X\n", uResponse);

			Sleep(1000);

			uResponse = SendKeyboard(hDriver, 0xED);
			printf("First response = 0x%X\n", uResponse);
			uResponse = SendKeyboard(hDriver, 0x00);
			printf("Second response = 0x%X\n", uResponse);
		}
		else if (!strcmp(pszArg, "id"))
		{
			printf("Getting keyboard type...");

			guKeyboardType = SendKeyboard(hDriver, 0xEB);

			switch (guKeyboardType)
			{
			case (unsigned)-1:
				printf("Error getting keyboard type: 0x%x\n", GetLastError());
				break;

			case 0xFA:
				printf("Omnikey keyboard, non-programmable?\n");
				break;

			case 0xFE:
				printf("Not an Omnikey or Avant keyboard.\n");
				break;

			case 0x17:
				printf("Keyboard is an Avant Stellar (or Prime?)\n");
				break;

			default:
				printf("Unknown keyboard type: 0x%X\n", guKeyboardType);
				break;
			}
		}
		else if (!strcmp(pszArg, "readkb"))
		{
			// Read keyboard programming into memory
			if (!ReadKeyboardProgram(hDriver))
				return -1;
		}
		else if (!strcmp(pszArg, "writefile"))
		{
			if (!gbGotProgram)
			{
				printf("No program has been loaded - please specify -readkb or -readfile first!\n");
				return -1;
			}

			--argc;
			++argv;

			if (argc < 1)
			{
				printf("Please supply the filename to write the keyboard program data into.\n");
				return -1;
			}

			if (!WriteProgramToFile(*argv))
				return -1;
		}
		else if (!strcmp(pszArg, "readfile"))
		{
			--argc;
			++argv;

			if (argc <= 1)
			{
				printf("Please supply the filename to read the keyboard program from.\n");
				return -1;
			}

			if (!ReadProgramFromFile(*argv))
				return -1;
		}
		else if (!strcmp(pszArg, "simulate"))
		{
			gbSimulationMode = TRUE;
		}
		else if (!strcmp(pszArg, "writekb"))
		{
			if (!gbGotProgram)
			{
				printf("No program has been loaded - please specify -readkb or -readfile first!\n");
				return -1;
			}

			// Write keyboard programming from memory
			if (!WriteProgramToKeyboard(hDriver))
				return -1;
		}
		else if (!strcmp(pszArg, "verbose"))
		{
			bVerbose = TRUE;
		}
		else if (!strcmp(pszArg, "print"))
		{
			PrintProgram(bVerbose);
		}
		else
		{
			printf("Invalid command: <%s>\n", argv[1]);

			return ShowUsage();
		}

		// On to next command, if any
		--argc;
		++argv;
		continue;
	}

	// That's it!
	return 0;
}

BOOL ValidateKeyboardType(HANDLE hDriver)
{
	unsigned uResponse;

	if ((uResponse = SendKeyboard(hDriver, KBD_ECHO)) != KBD_ECHO)
	{
		printf("Invalid echo reply: 0x%X!\n", uResponse);
		return FALSE;
	}

	guKeyboardType = SendKeyboard(hDriver, KBD_GET_MODEL);

	// TBD: find the ID for Prime and any others, and add support?
	if (guKeyboardType != KBD_ID_AVANT_STELLAR)
	{
		printf("Unsupported keyboard type: 0x%X!\n", guKeyboardType);
		return FALSE;
	}

	// Test the type again -- this is what the Avant software does
	if ((uResponse = SendKeyboard(hDriver, KBD_GET_MODEL)) != guKeyboardType)
	{
		printf("Mismatched keyboard type: 0x%X!\n", uResponse);
		return FALSE;
	}

	return TRUE;
}

/*
 *	ReadKeyboardProgram
 *
 *	Issues command bytes and reads responses in order to read the current programming from an Avant Stellar keyboard.
 *
 *	TBD: Test with a Prime to see if the protocol is the same.
 */

BOOL ReadKeyboardProgram(HANDLE hDriver)
{
	BOOL bSuccess = FALSE;

	unsigned uResponse;
	unsigned uKeyRepeat;
	unsigned uKeySlot;
	unsigned uMappedKey, uKeyFlags;
	unsigned uMacro, uUnknown;
	unsigned uKeystrokeIndex, uKeystroke;
	BOOL bTooManyKeystrokes;

	GetReady("read the keyboard programming");

	printf("Reading keyboard option...\n");

	memset(&gProgram, 0, sizeof(gProgram));
	memcpy(gProgram.header.cSig, KBD_HEADER_SIG, sizeof(gProgram.header.cSig));

	if (!ValidateKeyboardType(hDriver))
		return FALSE;

	// Begin the read-programming mode.  This will make the keyboard's lights all start flashing, and the keyboard won't be usable.
	if ((uResponse = SendKeyboard(hDriver, KBD_BEGIN_READ)) != KBD_CMD_ACK)
	{
		printf("Read-pgm ack failed: 0x%X!\n", uResponse);
		return FALSE;
	}

	// We're in read-program mode!  NOTE -- Beyond this point, if anything fails, we must exit the programming mode in order to make the keyboard work again!

	uKeyRepeat = SendKeyboard(hDriver, KBD_GET_REPEAT);

	// I think the high 4 bits are the delay (0-3) and the low 4 bits are the repeat rate
	gProgram.header.wKeyRepeatDelay = (uKeyRepeat >> 4) & 0x0F;
	gProgram.header.wKeyRepeatRate = (uKeyRepeat & 0x0F);

	// Read the comma/period lock flag
	uResponse = SendKeyboard(hDriver, KBD_GET_COMMA_LOCK);

	if (uResponse != 0x00 && uResponse != 0x01)
	{
		printf("Got bad comma/period lock value: 0x%X!\n", uResponse);
		goto __Bail_Out__;
	}

	gProgram.header.cCommaPeriodLock = (BYTE)uResponse;

	// Request the special-key modes... the *right* key flag byte comes back first, then the left!
	gProgram.header.cRightKeyModes = SendKeyboard(hDriver, KBD_GET_SPECIAL_MODES);
	gProgram.header.cLeftKeyModes = SendKeyboard(hDriver, KBD_GET_SPECIAL_MODES);

	printf("done.\nReading key mappings");

	/*
	 *	Read the key mapping array
	 */

	for (uKeySlot = 0; uKeySlot < N_KEY_SLOTS; ++uKeySlot)
	{
		printf(".");

		if ((uResponse = SendKeyboard(hDriver, KBD_GET_MAPPING)) != KBD_CMD_ACK)
		{
			printf("Read-mapping ack failed: 0x%X!\n", uResponse);
			goto __Bail_Out__;
		}

		uMappedKey = SendKeyboard(hDriver, (BYTE) uKeySlot);
		uKeyFlags = SendKeyboard(hDriver, (BYTE) uKeySlot);

		if (uMappedKey == (unsigned)-1 || uMappedKey > 0x7F)
		{
			printf("Invalid mapped key (0x%X) for key slot 0x%X!\n", uMappedKey, uKeySlot);
			goto __Bail_Out__;
		}

		if (uKeyFlags == (unsigned)-1 || uKeyFlags > 0x0F)
		{
			printf("Invalid key flags (0x%X) for key slot 0x%X!\n", uKeyFlags, uKeySlot);
			goto __Bail_Out__;
		}

		gProgram.keyMap[uKeySlot].cMappedTo = (BYTE) uMappedKey;
		gProgram.keyMap[uKeySlot].cFlags = (BYTE) uKeyFlags;
	}

	printf("done.\nReading macros");

	// Now read the macros
	for (uMacro = 0; uMacro < N_MACROS; ++uMacro)
	{
		// Reset warning flag on each macro
		bTooManyKeystrokes = FALSE;

		printf(".");

		if ((uResponse = SendKeyboard(hDriver, KBD_GET_MACRO)) != KBD_CMD_ACK)
		{
			printf("Read-mapping ack failed: 0x%X!\n", uResponse);
			goto __Bail_Out__;
		}

		// Key slot # the macro is mapped to, or 0x81 if the macro slot is unused
		uMappedKey = SendKeyboard(hDriver, (BYTE)uMacro);

		// Next byte is unknown but has always been 0x00 for used macro slots and 0x81 for unused ones... so let's enforce that until we find otherwise...
		uUnknown = SendKeyboard(hDriver, (BYTE)uMacro);

		// Index into saved keystroke list for this macro
		uKeystrokeIndex = 0;

		if (uMappedKey == KBD_FILE_END_OF_MACRO)
		{
			// Unused macro slot -- 2nd return byte should also be 0x81... or 0x00?
			if (uUnknown != KBD_FILE_END_OF_MACRO && uUnknown != 0x00)
			{
				printf("Invalid second key byte (0x%X) for macro 0x%X!\n", uUnknown, uMacro);
				goto __Bail_Out__;
			}

			// For an unused macro, we skip the get-keystroke loop below.  Everything else was initialized to zero above;
			// just leave it that way and add the terminator keystroke below.
		}
		else if (uMappedKey == (unsigned)-1 || uMappedKey > 0x7F)
		{
			printf("Invalid mapped key (0x%X) for macro 0x%X!\n", uMappedKey, uMacro);
			goto __Bail_Out__;
		}
		else
		{
			// Used macro slot
			gProgram.macros[uMacro].header.cKeySlot = (BYTE)uMappedKey;

			// Fetch the macro keystrokes
			while ((uKeystroke = SendKeyboard(hDriver, KBD_GET_MACRO_KEYSTROKE)) != KBD_END_OF_MACRO)
			{
				// Make sure we have room for more keystrokes... plus the 0x81 terminator
				if (uKeystrokeIndex >= N_MACRO_KEYSTROKES - 1)
				{
					if (!bTooManyKeystrokes)
					{
						printf("Warning: too many keystrokes returned for macro %u!  The rest are being dropped!\n", uMacro);

						// Don't warn again for this macro
						bTooManyKeystrokes = TRUE;
					}

					// Keep reading keystrokes until the end (TBD: or is this necessary?)
					continue;
				}

				uKeystroke = SendKeyboard(hDriver, KBD_GET_MACRO_KEYSTROKE);

				if (uKeystroke == KBD_END_OF_MACRO)
					break;

				gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot = (BYTE)uKeystroke;

				// Bump the keystroke count
				++uKeystrokeIndex;
			}
		}

		// For a used or unused macro slot, we always add the 0x81 terminator entry at the end
		gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot = KBD_FILE_END_OF_MACRO;

		// Store the final # of keystrokes in the macro, *including* the 0x81 terminator.
		gProgram.macros[uMacro].header.wNumKeystrokes = uKeystrokeIndex + 1;
	}

	printf("done.\n");

	gbGotProgram = TRUE;

	bSuccess = TRUE;

__Bail_Out__:

	if ((uResponse = SendKeyboard(hDriver, KBD_READWRITE_DONE)) != KBD_CMD_ACK)
	{
		printf("Exit-read-pgm ack failed: 0x%X!\n", uResponse);
		bSuccess = FALSE;
	}

	if (bSuccess)
		printf("Done reading keyboard.\n");
	else
		printf("Failed reading keyboard!\n");

	return bSuccess;
}

void PrintProgram(BOOL bVerbose)
{
	unsigned uIndex, uKeySlot, uFlagIndex, uMacro, uKeystrokeIndex, uKeystroke, uSpecial;
	BOOL bGotOne;
	int nIndex;
	char szMacroName[64];
	char szModifiers[128];

	// Send the current program in gProgram to stdout
	LPCSTR pszDashes = "===============================================================";

	printf("%s\n", pszDashes);

	printf("Keyboard Type:              %s\n",
		(guKeyboardType == KBD_ID_AVANT_STELLAR) ? "Avant Stellar" : "Unknown!");

	printf("\nAvant keyboard programming:\n");

	printf("Key Repeat Delay:           %s\n",
		gszKeyRepeatDelays[gProgram.header.wKeyRepeatDelay]);

	printf("Key Repeat Rate:            %s\n\n",
		gszKeyRepeatRates[gProgram.header.wKeyRepeatRate]);

	printf("Special Modes Enabled:\n");

	for (uIndex = 0, bGotOne = FALSE; gdwSpecialKeyModes[uIndex] != 0; ++uIndex)
	{
		if (gProgram.header.cLeftKeyModes & gdwSpecialKeyModes[uIndex])
		{
			printf("    Left %s\n", gpszSpecialKeyModes[uIndex]);
			bGotOne = TRUE;
		}

		if (gProgram.header.cRightKeyModes & gdwSpecialKeyModes[uIndex])
		{
			printf("    Right %s\n", gpszSpecialKeyModes[uIndex]);
			bGotOne = TRUE;
		}
	}

	if (!bGotOne)
		printf("    None.\n");

	printf("Comma/period lock: %s\n", gProgram.header.cCommaPeriodLock ? "On" : "Off");

	// Print the key mappings
	printf("\nMapped keys:\n");

	for (uKeySlot = 0, bGotOne = FALSE; uKeySlot < N_KEY_SLOTS; ++uKeySlot)
	{
		// Skip unmapped keys, unless we're in verbose mode
		if (gProgram.keyMap[uKeySlot].cMappedTo == uKeySlot && !bVerbose)
			continue;

		bGotOne = TRUE;

		szModifiers[0] = '\0';

		for (uFlagIndex = 0; gdwKeyMappingFlags[uFlagIndex] != 0; ++uFlagIndex)
		{
			if (gProgram.keyMap[uKeySlot].cFlags & gdwKeyMappingFlags[uFlagIndex])
			{
				if (szModifiers[0] != '\0')
					strncat(szModifiers, "+", sizeof(szModifiers));

				strncat(szModifiers, gpszKeyMappingFlags[uFlagIndex], sizeof(szModifiers));
			}
		}

		printf("<%s> --> %s<%s>\n", gpszKeySlots[uKeySlot], szModifiers, gpszKeySlots[gProgram.keyMap[uKeySlot].cMappedTo]);
	}

	if (!bGotOne)
		printf("    None.\n");

	printf("\nMacros:\n");

	for (uMacro = 0, bGotOne = FALSE; uMacro < N_MACROS; ++uMacro)
	{
		if (gProgram.macros[uMacro].header.cKeySlot == 0)
		{
			if (!bVerbose)
				continue;

			bGotOne = TRUE;
			printf("Slot %u: Unused.\n", uMacro);
			continue;
		}

		bGotOne = TRUE;

		// Format the macro name by removing trailing spaces
		memcpy(szMacroName, gProgram.macros[uMacro].header.szMacroName, sizeof(gProgram.macros[uMacro].header.szMacroName));

		for (nIndex = sizeof(gProgram.macros[uMacro].header.szMacroName) - 1; nIndex >= 0; --nIndex)
			if (szMacroName[nIndex] != ' ')
				break;

		szMacroName[nIndex + 1] = '\0';

		// TBD: print macro modifiers?  how do those work?
		printf("Slot %u: macro key = <%s>, name = <%s>.  Keystrokes:\n",
			uMacro,
			gpszKeySlots[gProgram.macros[uMacro].header.cKeySlot],
			szMacroName);

		for (uKeystrokeIndex = 0; uKeystrokeIndex < gProgram.macros[uMacro].header.wNumKeystrokes; ++uKeystrokeIndex)
		{
			uKeystroke = gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot;

			// First see if the keystroke is one of the macro special key values
			for (uSpecial = 0; gdwMacroSpecialKeys[uSpecial] != 0; ++uSpecial)
			{
				if (gdwMacroSpecialKeys[uSpecial] == uKeystroke)
				{
					printf("[%s] ", gpszMacroSpecialKeys[uSpecial]);
					break;
				}
			}

			if (gdwMacroSpecialKeys[uSpecial] == 0)
				printf("<%s> ", gpszKeySlots[uKeystroke]);
		}

		printf("\n");
	}

	if (!bGotOne)
		printf("    None.\n");
}

BOOL WriteProgramToFile(LPCSTR pszFile)
{
	unsigned uIndex, uBytes;
	MacroHeader fileHeader;
	char szName[KBD_MACRO_NAME_LEN + 1];

	int fd = _open(pszFile, O_BINARY | O_RDWR | O_CREAT | O_TRUNC, _S_IREAD | _S_IWRITE);

	if (fd == -1)
	{
		printf("Error %d creating file '%s'!\n", errno, pszFile);
		return -1;
	}

	// Write the file header
	if (_write(fd, &gProgram.header, sizeof(gProgram.header)) != sizeof(gProgram.header))
	{
		printf("Error %d writing file header to '%s'!\n", errno, pszFile);
		_close(fd);
		return -1;
	}

	// Write the key mappings
	for (uIndex = 0; uIndex < N_KEY_SLOTS; ++uIndex)
	{
		if (_write(fd, &gProgram.keyMap[uIndex], sizeof(gProgram.keyMap[uIndex])) != sizeof(gProgram.keyMap[uIndex]))
		{
			printf("Error %d writing key mapping struct to '%s'!\n", errno, pszFile);
			_close(fd);
			return -1;
		}
	}

	// Write the macros
	for (uIndex = 0; uIndex < N_MACROS; ++uIndex)
	{
		// Set up a local file-header struct because we have to tweak a couple things for the .kbd file format
		memset(&fileHeader, 0, sizeof(fileHeader));

		if (gProgram.macros[uIndex].header.cKeySlot == 0)
		{
			// Unused macro slot
			memset(fileHeader.szMacroName, ' ', sizeof(fileHeader.szMacroName));

			// 0x00 in the 3.10 version, 0xFF in the 4.00 version?
			fileHeader.cUnknown2 = 0xFF;
		}
		else
		{
			snprintf(szName, sizeof(szName), "%-*.*s", KBD_MACRO_NAME_LEN, KBD_MACRO_NAME_LEN, gProgram.macros[uIndex].header.szMacroName);

			memcpy(fileHeader.szMacroName, szName, sizeof(fileHeader.szMacroName));

		}

		fileHeader.wNumKeystrokes = gProgram.macros[uIndex].header.wNumKeystrokes;

		if (_write(fd, &fileHeader, sizeof(fileHeader)) != sizeof(fileHeader))
		{
			printf("Error %d writing macro header struct to '%s'!\n", errno, pszFile);
			_close(fd);
			return -1;
		}

		uBytes = gProgram.macros[uIndex].header.wNumKeystrokes * sizeof(MacroKeystroke);

		if (_write(fd, &gProgram.macros[uIndex].keystrokes, uBytes) != uBytes)
		{
			printf("Error %d writing macro keystrokes to '%s'!\n", errno, pszFile);
			_close(fd);
			return -1;
		}
	}

	_close(fd);
	return TRUE;
}

BOOL ReadProgramFromFile(LPCSTR pszFile)
{
	unsigned uKeyIndex, uMacroIndex, uKeystrokeIndex;
	MacroHeader fileHeader;
	WORD wKeystroke;

	int fd = _open(pszFile, O_BINARY | O_RDONLY);

	if (fd == -1)
	{
		printf("Error %d opening input file '%s'!\n", errno, pszFile);
		return -1;
	}

	// Read the file header
	if (_read(fd, &gProgram.header, sizeof(gProgram.header)) != sizeof(gProgram.header))
	{
		printf("Error %d reading file header from %s'!\n", errno, pszFile);
		_close(fd);
		return -1;
	}

	// Read the key mappings
	for (uKeyIndex = 0; uKeyIndex < N_KEY_SLOTS; ++uKeyIndex)
	{
		if (_read(fd, &gProgram.keyMap[uKeyIndex], sizeof(gProgram.keyMap[uKeyIndex])) != sizeof(gProgram.keyMap[uKeyIndex]))
		{
			printf("Error %d reading key mapping struct from '%s'!\n", errno, pszFile);
			_close(fd);
			return -1;
		}
	}

	// Read the macros
	for (uMacroIndex = 0; uMacroIndex < N_MACROS; ++uMacroIndex)
	{
		if (_read(fd, &fileHeader, sizeof(fileHeader)) != sizeof(fileHeader))
		{
			printf("Error %d reading macro header struct from '%s'!\n", errno, pszFile);
			_close(fd);
			return -1;
		}

		if (fileHeader.cKeySlot == 0)
		{
			// Unused macro slot -- make sure the other stuff matches
			if ((fileHeader.cUnknown2 != 0x00 && fileHeader.cUnknown2 != 0xFF) ||
				fileHeader.wNumKeystrokes != 1)
			{
				printf("Invalid unused macro entry (%u, %u) in file '%s'!\n", fileHeader.cUnknown2, fileHeader.wNumKeystrokes, pszFile);
				_close(fd);
				return -1;
			}
		}
		else
		{
			// Used macro slot... must have at least one keystroke (the 0x81 terminator) and no more than will fit in our array (without the terminator).
			if (fileHeader.wNumKeystrokes < 1 || fileHeader.wNumKeystrokes > N_MACRO_KEYSTROKES)
			{
				printf("Macro too large (%u keystrokes, max is %u) in file '%s'!\n", fileHeader.wNumKeystrokes, N_MACRO_KEYSTROKES, pszFile);
				_close(fd);
				return -1;
			}
		}

		gProgram.macros[uMacroIndex].header = fileHeader;

		for (uKeystrokeIndex = 0; uKeystrokeIndex < fileHeader.wNumKeystrokes; ++uKeystrokeIndex)
		{
			if (_read(fd, &wKeystroke, sizeof(wKeystroke)) != sizeof(wKeystroke))
			{
				printf("Error %d reading macro keystrokes from '%s'!\n", errno, pszFile);
				_close(fd);
				return -1;
			}

			if (uKeystrokeIndex == fileHeader.wNumKeystrokes - 1)
			{
				// The last keystroke should be the end-of-macro marker (0x81)
				if (wKeystroke != KBD_FILE_END_OF_MACRO)
				{
					printf("Invalid macro length or keystroke while reading macro keystrokes from '%s'!\n", pszFile);
					_close(fd);
					return -1;
				}
			}
			else
			{
				// Any other keystroke should -not- be the end-of-macro marker
				if (wKeystroke == KBD_FILE_END_OF_MACRO)
				{
					printf("Invalid macro length or unexpected end of macro while reading macro keystrokes from '%s'!\n", pszFile);
					_close(fd);
					return -1;
				}
			}

			// Store all the keystrokes, including the end-of-macro marker
			memcpy(&gProgram.macros[uMacroIndex].keystrokes[uKeystrokeIndex], &wKeystroke, sizeof(wKeystroke));
		}
	}

	gbGotProgram = TRUE;

	_close(fd);
	return TRUE;
}

/*
 *	The Holy Grail and the point of this whole large exercise: write the currently-loaded program out to the Avant keyboard,
 *	so our poor, patient user can finally USE it! :)
 */

BOOL WriteProgramToKeyboard(HANDLE hDriver)
{
	BOOL bSuccess = FALSE;

	unsigned uResponse;
	unsigned uKeyRepeat;
	unsigned uKeySlot;
	unsigned uMacro;
	unsigned uKeystrokeIndex;
	BYTE cMacro1, cMacro2;

	GetReady("write the current programming to the keyboard");

	printf("Setting up to write keyboard...\n");

	if (!ValidateKeyboardType(hDriver))
		return FALSE;

	// Use simulation mode starting now, if it was requested
	gbEnableSimulation = TRUE;

	if ((uResponse = SendKeyboard(hDriver, KBD_BEGIN_WRITE)) != KBD_CMD_ACK)
	{
		printf("Read-pgm ack failed: 0x%X!\n", uResponse);
		return FALSE;
	}

	printf("ok.\nWriting options...");

	// We're in write-program mode!  NOTE -- Beyond this point, if anything fails, we must exit the programming mode in order to make the keyboard work again!
	// If we otherwise bail out in the middle, the keyboard will probably be usable with the partial programming we've sent so far -- I don't see any reason
	// why not, at least...

	uKeyRepeat = ((gProgram.header.wKeyRepeatDelay & 0x0F) << 4) | (gProgram.header.wKeyRepeatRate & 0x0F);

	if ((uResponse = SendKeyboard(hDriver, KBD_SET_REPEAT)) != KBD_CMD_ACK ||
		(uResponse = SendKeyboard(hDriver, (BYTE)uKeyRepeat)) != KBD_CMD_ACK)
	{
		printf("Write key-repeat failed: 0x%X!\n", uResponse);
		goto __Bail_Out__;
	}

	// Send the comma/period lock flag
	if ((uResponse = SendKeyboard(hDriver, KBD_SET_COMMA_LOCK)) != KBD_CMD_ACK ||
		(uResponse = SendKeyboard(hDriver, (BYTE)gProgram.header.cCommaPeriodLock)) != KBD_CMD_ACK)
	{
		printf("Write comma/period lock failed: 0x%X!\n", uResponse);
		goto __Bail_Out__;
	}

	// Send the special key modes -- when setting these, the left key byte comes first, then the right (we read them in the opposite order!)
	if ((uResponse = SendKeyboard(hDriver, KBD_SET_SPECIAL_MODES)) != KBD_CMD_ACK ||
		(uResponse = SendKeyboard(hDriver, (BYTE)gProgram.header.cLeftKeyModes)) != KBD_CMD_ACK ||
		(uResponse = SendKeyboard(hDriver, (BYTE)gProgram.header.cRightKeyModes)) != KBD_CMD_ACK)
	{
		printf("Write special key modes failed: 0x%X!\n", uResponse);
		goto __Bail_Out__;
	}

	printf("done.\nWriting macros");

	for (uMacro = 0; uMacro < N_MACROS; ++uMacro)
	{
		printf(".");

		// The unknown byte after the macro mapped key slot is 0x00 for a used macro slot, 0x81 for an unused one, apparently...
		if (gProgram.macros[uMacro].header.cKeySlot == 0)
		{
			// Unused - both bytes are 0x81
			cMacro1 = cMacro2 = KBD_FILE_END_OF_MACRO;
		}
		else
		{
			// Used - first byte is the key slot, second byte is 0x00
			cMacro1 = gProgram.macros[uMacro].header.cKeySlot;
			cMacro2 = 0x00;
		}

		// If we send too many keystrokes, I don't know if it could overrun the macro memory, so let's be safe...
		if (gProgram.macros[uMacro].header.wNumKeystrokes > N_MACRO_KEYSTROKES)
		{
			printf("Internal error: invalid macro %u data, cannot continue!\n", uMacro);
			goto __Bail_Out__;
		}

		// Now write the list of keystrokes.  The 0x81 is stored in our in-memory list of keystrokes, but the 0x82 is not!  Let's make sure of all that, before we start...
		for (uKeystrokeIndex = 0; uKeystrokeIndex < gProgram.macros[uMacro].header.wNumKeystrokes; ++uKeystrokeIndex)
		{
			BOOL bShouldBeEnd = (uKeystrokeIndex == gProgram.macros[uMacro].header.wNumKeystrokes - 1);
			BOOL bIsEnd = (gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot == KBD_FILE_END_OF_MACRO);
			BOOL bIsEnd2 = (gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot == KBD_END_OF_MACRO);

			if ((bShouldBeEnd != bIsEnd) || bIsEnd2)
			{
				printf("Internal error: invalid macro %u data, cannot continue!\n", uMacro);
				goto __Bail_Out__;
			}
		}

		if ((uResponse = SendKeyboard(hDriver, KBD_WRITE_MACRO)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, (BYTE)uMacro)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, cMacro1)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, cMacro2)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, KBD_WRITE_MACRO_KEYSTROKE)) != KBD_CMD_ACK)
		{
			printf("Write macro %u header failed: 0x%X!\n", uMacro, uResponse);
			goto __Bail_Out__;
		}

		// Send the list of keystrokes -- including the 0x81 first terminator, which we verified above is present!
		for (uKeystrokeIndex = 0; uKeystrokeIndex < gProgram.macros[uMacro].header.wNumKeystrokes; ++uKeystrokeIndex)
		{
			if ((uResponse = SendKeyboard(hDriver, gProgram.macros[uMacro].keystrokes[uKeystrokeIndex].cKeySlot)) != KBD_CMD_ACK)
			{
				printf("Write macro %u keystroke failed: 0x%X!\n", uMacro, uResponse);
				goto __Bail_Out__;
			}
		}

		// Write the 0x82 marking the end of the macro in the keyboard protocol
		if ((uResponse = SendKeyboard(hDriver, KBD_END_OF_MACRO)) != KBD_CMD_ACK)
		{
			printf("Write macro %u header failed: 0x%X!\n", uMacro, uResponse);
			goto __Bail_Out__;
		}
	}

	printf("done.\nWriting key mappings");

	if ((uResponse = SendKeyboard(hDriver, KBD_SET_MAPPING)) != KBD_CMD_ACK)
	{
		printf("Write macro %u header failed: 0x%X!\n", uMacro, uResponse);
		goto __Bail_Out__;
	}

	for (uKeySlot = 0; uKeySlot < N_KEY_SLOTS; ++uKeySlot)
	{
		printf(".");

		// The key mappings go out in groups of 3 bytes: the key slot #, the mapped key slot # and the modifiers byte
		if ((uResponse = SendKeyboard(hDriver, (BYTE)uKeySlot)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, gProgram.keyMap[uKeySlot].cFlags)) != KBD_CMD_ACK ||
			(uResponse = SendKeyboard(hDriver, gProgram.keyMap[uKeySlot].cMappedTo)) != KBD_CMD_ACK)
		{
			printf("Write key mapping %u failed: 0x%X!\n", uKeySlot, uResponse);
			goto __Bail_Out__;
		}
	}

	if ((uResponse = SendKeyboard(hDriver, KBD_SET_MAPPING_DONE)) != KBD_CMD_ACK)
	{
		printf("End key mapping failed: 0x%X!\n", uResponse);
		goto __Bail_Out__;
	}

	printf("done.\n");

	bSuccess = TRUE;

__Bail_Out__:

	if ((uResponse = SendKeyboard(hDriver, KBD_READWRITE_DONE)) != KBD_CMD_ACK)
	{
		printf("Exit-write-pgm ack failed: 0x%X!\n", uResponse);
		bSuccess = FALSE;
	}

	// Done with simulation mode, if it was enabled to begin with
	gbEnableSimulation = FALSE;

	if (bSuccess)
		printf("Done writing keyboard.\n");
	else
		printf("Failed writing keyboard!\n");

	return bSuccess;
}

