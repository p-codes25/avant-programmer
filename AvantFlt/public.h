#ifndef _PUBLIC_H
#define _PUBLIC_H

#if OLD_OLD_OLD

//
// Used to identify kbfilter bus. This guid is used as the enumeration string
// for the device id.
DEFINE_GUID(GUID_BUS_KBFILTER,
0xa65c87f9, 0xbe02, 0x4ed9, 0x92, 0xec, 0x1, 0x2d, 0x41, 0x61, 0x69, 0xfa);
// {A65C87F9-BE02-4ed9-92EC-012D416169FA}

DEFINE_GUID(GUID_DEVINTERFACE_KBFILTER,
0x3fb7299d, 0x6847, 0x4490, 0xb0, 0xc9, 0x99, 0xe0, 0x98, 0x6a, 0xb8, 0x86);
// {3FB7299D-6847-4490-B0C9-99E0986AB886}

#endif

// These are new GUIDs created for the avantflt driver.  We do NOT want to use
// the ones from the WDK sample!

DEFINE_GUID(GUID_BUS_KBFILTER,
0xEB850E15, 0xBD43, 0x4C52, 0xB0, 0x4D, 0x18, 0x65, 0x3C, 0x77, 0x34, 0x08);
// {EB850E15-BD43-4C52-B04D-18653C773408}


#define  KBFILTR_DEVICE_ID L"{EB850E15-BD43-4C52-B04D-18653C773408}\\KeyboardFilter\0"


DEFINE_GUID(GUID_DEVINTERFACE_KBFILTER,
0x34839DE2, 0x2E1C, 0x4F27, 0xA0, 0x85, 0x11, 0x8C, 0xEC, 0xB1, 0xDA, 0x6A);
// {34839DE2-2E1C-4F27-A085-118CECB1DA6A}

//
// IOCTL Related defintions
//

// #define IOCTL_INDEX             0x800

// From the original kbfiltr sample
#define IOCTL_KBFILTR_GET_KEYBOARD_ATTRIBUTES CTL_CODE( FILE_DEVICE_KEYBOARD,   \
                                                        0x800,    \
                                                        METHOD_BUFFERED,    \
                                                        FILE_READ_DATA)

// Our new ioctl for programming Avant keyboards
#define IOCTL_KBFILTR_KEYBOARD_SEND_BYTE CTL_CODE( FILE_DEVICE_KEYBOARD,   \
                                                        0x801,    \
                                                        METHOD_BUFFERED,    \
                                                        FILE_READ_DATA)

#endif

