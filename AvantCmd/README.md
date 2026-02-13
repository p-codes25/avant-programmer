# AvantCmd

This is a Win32/Win64 command-line utility which reads, writes and displays programming information for Avant keyboards.

It uses the AvantFlt keyboard filter driver to communicate with the keyboard; so, the AvantFlt driver must be installed and running in order to use this utility.

This utility basically does the job of CVT's old 'avantdwn.exe' utility for DOS (as far as writing programming data to the Avant keyboard) as well as the 'upload' function of CVT's old Win9x software, which was used to read the current programming information stored in an Avant keyboard.

The benefit of this utility and the AvantFlt driver is that they run on Windows 7 and Windows 10, and should run on Windows 11, so you don't have to dig your old DOS system out of storage in order to reprogram your Avant keyboard!

## Command-Line Usage

The following command-line functions are standalone and may not be combined with
 other options:
 
- avantcmd -send byte1 byte2 ...

    Sends specified bytes to keyboard and prints the response byte for each one.
    Specify bytes in hex with no 0x prefix or h suffix.  For example:
    avantcmd -send EE EB

- avantcmd -sendfile file.ext

    Reads bytes from specified file and sends them to the keyboard, printing the response
    byte for each.

- avantcmd -flash

    Flashes the keyboard lights on and off once.

- avantcmd -id

    Sends the Avant keyboard-ID command and prints the response, indicating whether the keyboard was detected as an Avant Stellar, Avant Prime (although I haven't tested it on one of those), a non-programmable Omnikey keyboard, or some other/unknown type.

The following functions may be combined on the command line:

- -readkb

    Reads the current programming from the keyboard into memory.

- -readfile file.kbd

    Reads programming data from the specified .kbd file into memory.

- -verbose

    Use before -print to print all key mappings, including standard ones.

- -print

    Prints the currently-loaded programming data to stdout.

- -simulate

    Does not actually send programming commands to keyboard; for testing.

- -writekb

    Writes the currently-loaded programming data to the keyboard.

- -writefile file.kbd

    Writes the currently-loaded programming data to the specified .kbd file.

## Examples

- avantcmd -readkb -writefile mykeyboard.kbd

    Reads the keyboard's current programming, and saves it to the file 'mykeyboard.kbd'.

- avantcmd -readfile newprogram.kbd -writekb

    Reads the file 'newprogram.kbd' and writes its programming to the keyboard.

- avantcmd -readkb -print

    Reads the keyboard's current programming and prints it to stdout.
