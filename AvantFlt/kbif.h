
//
// Functions in kbif.c - low-level i8042 port interface routines
//

int DoKeyboardSendReceive(BYTE cRequest, BYTE *pcResponse);
int StartDisableScanCodeTranslation();
int DisableScanCodeTranslationStep3(BYTE *pcCommandByte);
int SetCommandBytePart1(BYTE cCommandByte);
int SetCommandByte(BYTE cCommandByte);
