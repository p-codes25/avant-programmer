This project consists of a Windows utility, driver and documentation for programming the old Avant Stellar and Prime mechanical keyboards from Creative Vision Technology, Inc.

General information about these keyboards, their history, hardware, software, etc. is documented on Deskthority:

https://deskthority.net/wiki/CVT_Avant_Stellar

Why does anyone still use these old keyboards?  Well, aside from their solid build and nice clicky mechanical keyswitches, these keyboards have the rare ability to have the keyboard layout (i.e. key-to-scancode mappings) changed and saved permanently to an EEPROM (or flash memory) chip built into the keyboard.  You can also program macros and other settings to this permanent memory, and the programming is done via DOS/Windows software that CVT provided.

There are several reasons I decided to put this software together:

- CVT (which has been out of business since the early 2000s) never produced any software that could program their keyboards from a computer running a Windows version later than XP, and hardly anyone runs XP or older OSes anymore;

- I have used Avant keyboards for decades, and occasionally they will lose their programming (reverting to the factory default key mappings and other settings), and also I would occasionally need to make some change to the keyboard programming;

- It seemed like a fun project (and indeed it was!);

- It seemed like a good stepping stone to figuring out how to support programming these keyboards through a USB port (but I have not tackled that project yet).

I'm providing this code for informational purposes for anyone who wants to understand these keyboards' communication protocol, or who wants to know about writing Windows keyboard filter drivers.

As far as actually using this software, there are a couple of major caveats:

1. Actually programming an Avant keyboard must be done on a PC that has a physical PS/2 keyboard port (a combo PS/2 keyboard/mouse port should work too).  It won't work on an Avant keyboard connected through a USB-to-PS/2 converter, and it won't work in a VM guest, as far as I've found, because apparently hypervisors (at least VMWare Workstation) don't virtualize the i8042 controller chip fully enough for this software to do what it needs to do.

2. In order to use the keyboard filter, either your system must be booted in 'test signing mode' where Windows does not verify the root code-signing certificate of drivers (and this is *not* recommended on security principles), or else you must build the driver yourself and have it code-signed by a company that writes device drivers.  This is all due to Microsoft's more stringent driver signing requirements that came into effect in the late 2010s or so.  It effectively takes away the ability of home/hobbyist driver writers to produce signed drivers that will run on their own systems :(

