
/*
 *	Structures and constants used in reading/writing the .kbd file format.
 *
 *	See also: Docs\AvantKBDFileLayout.txt
 */

#pragma pack(push, 1)

typedef struct tagKBDFileHeader
{
								// Offset - Comments
	BYTE	cSig[3];			// 0x00 - "KBD"
	WORD	wKeyRepeatRate;		// 0x03 - See AVANT_KEY_REPEAT_RATE below
	WORD	wKeyRepeatDelay;	// 0x05 - See AVANT_KEY_REPEAT_DELAY below
	WORD	wUnknown1;			// 0x07
	BYTE	cLeftKeyModes;		// 0x09 -- See AVANT_SPECIAL_KEY_MODES below
	BYTE	cUnknown4;			// 0x0A - Always zero (or cLeftKeyModes is a WORD?)
	BYTE	cRightKeyModes;		// 0x0B -- See AVANT_SPECIAL_KEY_MODES below
	BYTE	cUnknown5;			// 0x0C - Always zero (or cRightKeyModes is a WORD?)
	BYTE	cCommaPeriodLock;	// 0x0D - 0 if off, 1 if on
	WORD	wUnknown2;			// 0x0E
	BYTE	cUnknown3[3];		// 0x10
} KBDFileHeader;				// 0x13 bytes total

#define KBD_HEADER_SIG			"KBD"

// wKeyRepeatRate values

#define AVANT_KEY_REPEAT_RATE \
	ASSOC(0x00,	"1 / sec") \
	ASSOC(0x01,	"4 / sec") \
	ASSOC(0x02,	"6 / sec") \
	ASSOC(0x03,	"8 / sec") \
	ASSOC(0x04,	"10 / sec") \
	ASSOC(0x05,	"15 / sec") \
	ASSOC(0x06,	"20 / sec") \
	ASSOC(0x07,	"25 / sec") \
	ASSOC(0x08,	"30 / sec") \
	ASSOC(0x09,	"43 / sec") \
	ASSOC(0x0A,	"63 / sec") \
	ASSOC(0x0B,	"125 / sec") \
	ASSOC(0x0C,	"300 / sec") \


#undef ASSOC
#define ASSOC(by, str)		str,

static LPCSTR gszKeyRepeatRates[] =
{
	AVANT_KEY_REPEAT_RATE
	NULL
};


#define AVANT_KEY_REPEAT_DELAY \
	ASSOC(0x00,	"Invalid") \
	ASSOC(0x01,	"0.25s") \
	ASSOC(0x02,	"0.50s") \
	ASSOC(0x03,	"0.75s") \
	ASSOC(0x04,	"1.00s") \
	ASSOC(0x05,	"1.25s") \
	ASSOC(0x06,	"1.50s") \
	ASSOC(0x07,	"1.85s") \

#undef ASSOC
#define ASSOC(by, str)		str,

static LPCSTR gszKeyRepeatDelays[] =
{
	AVANT_KEY_REPEAT_DELAY
	NULL
};


// Bit values for dwSpecialKeyModes

#define AVANT_SPECIAL_KEY_MODES \
	ASSOC(0x01,		"Alt Sticky") \
	ASSOC(0x02,		"Alt Locked") \
	ASSOC(0x10,		"Shift Sticky") \
	ASSOC(0x20,		"Shift Locked") \
	ASSOC(0x40,		"Ctrl Sticky") \
	ASSOC(0x80,		"Ctrl Locked") \

#undef ASSOC
#define ASSOC(val, str)		val,

static DWORD gdwSpecialKeyModes[] =
{
	AVANT_SPECIAL_KEY_MODES
	0L
};

#undef ASSOC
#define ASSOC(val, str)		str,

static LPCSTR gpszSpecialKeyModes[] =
{
	AVANT_SPECIAL_KEY_MODES
	NULL
};



// Key remapping values - this indicate the numeric slot # of each actual key,
// and also give the slot # that a key is remapped to, if any.  There are 128
// key slots that may be remapped, but not all of them are used on the physical
// keyboards.
//
// These slot indexes might be related to the keyboard matrix rows/columns, but
// I haven't checked.  They have no relation to the scan codes (set 1 or set 2)
// that I can see.

#define N_KEY_SLOTS		128

static LPCSTR gpszKeySlots[N_KEY_SLOTS] =
{
	"n/a",			// 0x00 - Marks unused macro slot in the macro table
	"n/a",
	"F9",
	"F7",
	"F5",
	"F3",
	"F1",
	"F11",

	"n/a",			// 0x08 - Used for "Delay" in macros
	"n/a",
	"F10",
	"F8",
	"F6",
	"F4",
	"F2",
	"F12",

	"NumPad 0",		// 0x10
	"NumPad 2",
	"n/a",
	"n/a",
	"Caps Lock",	// In its original location, to the left of A
	"Tab",
	"`",			// Backquote / tilde in its original locatino to the left of 1 / !
	"Esc",			// In its original position between F12 and SF1

	"NumPad .",		// 0x18
	"NumPad 3",
	"n/a",
	"Z",
	"A",
	"Q",
	"1",
	"SF1",

	"NumPad Enter",	// 0x20
	"NumPad 4",
	"n/a",
	"X",
	"S",
	"W",
	"2",
	"SF2",

	"PrtSc",		// 0x28
	"NumPad 5",
	"n/a",
	"C",
	"D",
	"E",
	"3",
	"SF3",

	"ScrLk",		// 0x30
	"NumPad 6",
	"n/a",
	"V",
	"F",
	"R",
	"4",
	"SF4",

	"Pause",		// 0x38
	"NumPad =",
	"Space Bar",
	"B",
	"G",
	"T",
	"5",
	"SF5",

	"NumPad -",		// 0x40
	"NumPad +",
	"NumPad 1",
	"\\",
	"Enter",
	"]",
	"=",
	"SF12",

	"NumPad *",		// 0x48
	"NumPad 9",
	"Backspace",
	"Menu",			// The "Menu" key below right shift
	"'",			// Normal (single) quote / double quote
	"[",
	"-",
	"SF11",

	"NumPad /",		// 0x50
	"NumPad 8",
	"Up Arrow",
	"/",
	";",
	"P",
	"0",			// Number 0 / )
	"SF10",

	"NumLk",		// 0x58
	"NumPad 7",
	"Right Arrow",
	".",
	"L",
	"O",			// Letter O
	"9",
	"SF9",

	"PgUp",			// 0x60
	"PgDn",
	"Down Arrow",
	",",
	"K",
	"I",
	"8",
	"SF8",

	"Home",			// 0x68
	"End",
	"Left Arrow",
	"M",
	"J",
	"U",
	"7",
	"SF7",

	"Ins",			// 0x70
	"Del",
	"n/a",
	"N",
	"H",
	"Y",
	"6",
	"SF6",

	"Right Ctrl",	// 0x78 - In its original location below the backslash (\) / vertical bar (|) key
	"Right Shift",
	"Right WinKey",
	"Right Alt",	// In its original location next to the spacebar
	"Left Alt",		// In its original location next to the spacebar
	"Left WinKey",
	"Left Shift",
	"Left Ctrl"		// In its original location below the left shift key
};

// There are 128 of these, one each for the key slots 0x00 - 0x7F above
typedef struct tagKeyMapping
{
	BYTE	cMappedTo;	// Key slot value (0x00 - 0x7F) that the key in this slot is mapped to;
						// equals the key's slot index itself, if the key isn't remapped.
	BYTE	cUnknown1;	// Always 0?
	BYTE	cFlags;		// See KEY_MAPPING_FLAGS below
	BYTE	cUnknown2;	// Always 0?
} KeyMapping;


#define KEY_MAPPING_FLAGS \
	ASSOC(0x01,	"Shift") \
	ASSOC(0x02,	"Ctrl") \
	ASSOC(0x04,	"Alt") \
	ASSOC(0x08,	"No Repeat") \

#undef ASSOC
#define ASSOC(val, str)		val,

static DWORD gdwKeyMappingFlags[] =
{
	KEY_MAPPING_FLAGS
	0x00
};

#undef ASSOC
#define ASSOC(val, str)		str,

static LPCSTR gpszKeyMappingFlags[] =
{
	KEY_MAPPING_FLAGS
	NULL
};

#define KBD_MACRO_NAME_LEN		20

typedef struct tagMacroHeader
{
	BYTE	cKeySlot;							// Key mapping slot index mapped to this macro (0x00 in the .kbd file if unused -- same in this struct!)
	BYTE	cUnknown1;							// Always 0x00?
	BYTE	cModifiers;							// TBD
	BYTE	cUnknown2;							// Always 0x00 in the 3.10 version?  Always 0xFF in the 4.0 version?
	BYTE	szMacroName[KBD_MACRO_NAME_LEN];	// Last char is always a space (NOT a null byte!)  This field does *not* exist in the keyboard protocol -- only in the .kbd file.
	WORD	wNumKeystrokes;						// Number of MacroKeystroke structs to follow (note -- includes the 0x81 terminator entry, and so is always at least 1!)
} MacroHeader;

typedef struct tagMacroKeystroke
{
	BYTE	cKeySlot;			// Key slot index for this keystroke in the macro; see special values below; and, 0x81 to mark the end of the sequence.
	BYTE	cUnknown;			// In the keyboard protocol, this is always 0x00 for used macro slots, and 0x81 for unused ones?
} MacroKeystroke;

// TBD: are these truly the same as the regular key slots?
#define MACRO_SPECIAL_KEYS \
	ASSOC(0x08,	"Delay") \
	ASSOC(0x78,	"Right Ctrl Down") \
	ASSOC(0x79,	"Right Shift Down") \
	ASSOC(0x7B,	"Right Alt Down") \
	ASSOC(0x7C,	"Left Alt Down") \
	ASSOC(0x7E,	"Left Shift Down") \
	ASSOC(0x7F,	"Left Ctrl Down") \
	ASSOC(0x81, "End of Macro") \
	ASSOC(0xF8,	"Right Ctrl Up") \
	ASSOC(0xF9,	"Right Shift Up") \
	ASSOC(0xFB,	"Right Alt Up") \
	ASSOC(0xFE,	"Left Shift Up") \
	ASSOC(0xFF,	"Left Ctrl Up") \

#undef ASSOC
#define ASSOC(val, str)		val,

static DWORD gdwMacroSpecialKeys[] =
{
	MACRO_SPECIAL_KEYS
	0x00
};

#undef ASSOC
#define ASSOC(val, str)		str,

static LPCSTR gpszMacroSpecialKeys[] =
{
	MACRO_SPECIAL_KEYS
	NULL
};

#pragma pack(pop)

/*
 *	Structures for in-memory definition of keyboard program
 */

#define N_MACRO_KEYSTROKES	69		// Supposedly the maximum # keystrokes per macro, per the Avant on-line help and/or manual
#define N_MACROS			24		// Or 48??

typedef struct tagMacro
{
	MacroHeader header;
	MacroKeystroke keystrokes[N_MACRO_KEYSTROKES];
} Macro;

typedef struct tagKeyboardProgram
{
	// Keyboard options
	KBDFileHeader header;

	// Key mappings
	KeyMapping keyMap[N_KEY_SLOTS];

	// Macro definitions
	Macro macros[N_MACROS];

} KeyboardProgram;

// The 0xEE is a standard PS/2 keyboard command; the rest are Avant-specific...

#define AVANT_KEYBOARD_COMMANDS \
	ASSOC(0xEE,		KBD_ECHO) \
	ASSOC(0xEB,		KBD_GET_MODEL) \
	ASSOC(0xD6,		KBD_BEGIN_READ) \
	ASSOC(0xDD,		KBD_GET_REPEAT) \
	ASSOC(0xDB,		KBD_GET_VALIDATION) \
	ASSOC(0xDA,		KBD_GET_COMMA_LOCK) \
	ASSOC(0xDF,		KBD_GET_SPECIAL_MODES) \
	ASSOC(0xE0,		KBD_GET_MAPPING) \
	ASSOC(0xE9,		KBD_GET_MACRO) \
	ASSOC(0xE1,		KBD_GET_MACRO_KEYSTROKE) \
	ASSOC(0xE6,		KBD_READWRITE_DONE) \
	ASSOC(0xE7,		KBD_BEGIN_WRITE) \
	ASSOC(0xE3,		KBD_SET_VALIDATION) \
	ASSOC(0xE4,		KBD_SET_REPEAT) \
	ASSOC(0xE5,		KBD_WRITE_MACRO) \
	ASSOC(0xE2,		KBD_WRITE_MACRO_KEYSTROKE) \
	ASSOC(0xE8,		KBD_SET_SPECIAL_MODES) \
	ASSOC(0xEA,		KBD_SET_MAPPING) \
	ASSOC(0xFF,		KBD_SET_MAPPING_DONE) \
	ASSOC(0xEC,		KBD_SET_COMMA_LOCK) \

#undef ASSOC
#define ASSOC(val, label)	label = val,

typedef enum eKeyboardCommand
{
	AVANT_KEYBOARD_COMMANDS
} KeyboardCommand;

// Response to KBD_GET_MODEL for the Avant Stellar... I don't know what value the Prime returns.
#define KBD_ID_AVANT_STELLAR		0x17

// Acknowledge response from the keyboard (standard PS/2 keyboard ack value)
#define KBD_CMD_ACK					0xFA

#define KBD_FILE_END_OF_MACRO		0x81		// End-of-macro marker in the .kbd file and in the keyboard-write protocol
#define KBD_END_OF_MACRO			0x82		// End-of-macro marker in the keyboard-write protocol

