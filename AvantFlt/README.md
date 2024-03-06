# AvantFlt

Keyboard filter kernel-mode-driver for Windows 7 and later (possibly Vista too) 32-bit or 64-bit versions, for programming Avant keyboards.

This was based on the WDK kbfiltr sample driver from the Win7 WDK.

## Prerequisites

- WDK for Windows 7 (WDK version 7600.16385.1) installed (the driver should work with later WDK versions, but I haven't tested any).

## Building the Driver

- Open a WDK x86 or x64 checked or free build environment window, as desired;

- Change to the AvantFlt directory;

- Run:

	build -ceZ

That produces the base driver binary, not code-signed.  For 64-bit Windows operating systems, you will need to code-sign the driver in one of two ways:

1. Using a test certificate, in which case you will have to permanently set your system to "test signing" mode.  This disables the requirement for kernel-mode driver certificate chains to terminate in a Microsoft signature, and is NOT recommended because it really is a security risk for your system.

2. Using the Microsoft driver signing portal, which requires an EV certificate issued to a company for use in Microsoft's driver signing process.  This is not feasible for most individual hobbyists, which is unfortunate but it's Microsoft's policy.

## Test-Signing the Driver

If you want to use the test-signing method (again, NOT recommended, as it's a security risk for your system), use these steps:

1. Create a test certificate, as described here:

	https://learn.microsoft.com/en-us/windows-hardware/drivers/install/test-signing

	I used commands similar to these:

	makecert -r -pe -ss PrivateCertStore -n CN=MyTestCert -eku 1.3.6.1.5.5.7.3.3 MyTestCert.cer

	certmgr /add MyTestCert.cer /s /r localMachine root

2. Copy the following files from the build output directory (for example, objchk_win7_amd64\amd64
	under your source directory) to a new, empty temporary directory:

	avantflt.sys
	avantflt.pdb
	avantflt.inf

	Also copy the WDF co-installer DLL from the DDK (for example, C:\WinDDK\7600.16385.1\redist\wdf\amd64)
	to the new temporary directory:

	WdfCoInstaller01009.dll

3. Build a catalog file for the driver (avantflt.cat), by running inf2cat:

	inf2cat /driver:. /os:7_X64

4. Sign the driver package:

	Signtool sign /v /fd sha256 /s PrivateCertStore /n MyTestCert avantflt.cat

Now, you can put your system into test-signing mode, and you will be able to install the driver.

## Installing the Driver

I used the manual method of installing the device driver through Device Manager; Microsoft also has
automatic deployment methods here:

https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/keyboard-input-wdf-filter-driver-kbfiltr/

But I haven't tried those.  What I did was:

1. Open Device Manager.
2. Go to Keyboards -> PS/2 Keyboard.
3. Right-click on the keyboard and choose Update Driver Software...
4. Choose "Browse my computer for driver software".
5. Choose "Let me pick from a list of device drivers on my computer".
6. Choose "Have Disk..."
7. Browse to the directory you created above, with the avantflt.sys and .inf files.
8. The driver list should now show "Filter Driver For Programming Avant Keyboards".  Select that, and click Next.
9. If the driver is test-signed, you'll get warnings about it not being signed; click to accept/continue on those.
10. Once the driver is installed, you'll be prompted to reboot the system.  Once you do, then Device Manager
	should show under Keyboards, the "Filter Device For Programming Avant Keyboards".

Once the driver is installed and running, you can use the AvantCmd utility to read/write your Avant keyboard's programming!
