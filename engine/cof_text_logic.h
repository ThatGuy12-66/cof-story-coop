/*
cof_text_logic.h - Cry of Fear text beyond one byte per glyph: code pages, UTF-8, line breaks
GPL-3.0-or-later.

Cry of Fear's text pipeline is 8-bit: the game DLLs, the language packs and every font path hand text on one byte
at a time. A language pack names its code page (manifest.txt `codepage`, the cvar cof_text_codepage): 1250 (Polish),
1251 (Cyrillic) and 1252 (Western) are one byte per character; 65001 is UTF-8, for scripts no single-byte page holds
(Simplified Chinese). This header is everything the engine font paths (engine/client/cof_glyphs.c, cl_font.c), the
VGUI library (3rdparty/freevgui/platform/xash3d-fwgs) and the offline tests agree on:

- a byte of a single-byte page to Unicode, and Unicode to a cp1252 cell (the engine's atlases are cp1252);
- UTF-8: the next character of a string (CoF_Text_Next), and a decoder fed one byte at a time (CoF_Text_Feed) for the
  paths that only ever see one byte;
- the per-byte cells of UTF-8 text: client.dll lays text out byte by byte from a 256-entry width table, so in UTF-8
  a lead byte carries the whole character's cell and its continuation bytes none (CoF_Text_Utf8ByteCell);
- where a line may break: Chinese has no spaces, so every wide character is a place to break, except before closing
  punctuation and after opening punctuation (CoF_Text_BreakBetween).

"Tolerant" reading: text in cp1252 that holds a complete, valid UTF-8 multi-byte sequence is read as UTF-8 there (a
Chinese host's map text, or a chat line, reaching an English player). Only for cp1252: in cp1250 and cp1251 ordinary
words form valid sequences ("Ół", "Р»"), in cp1252 they essentially never do.

Pure: no engine headers; C and C++. GPL-3.0-or-later like the engine; the code page tables are facts.
*/
#ifndef COF_TEXT_LOGIC_H
#define COF_TEXT_LOGIC_H

#include <stdint.h>

#define COF_CP_UTF8 65001

// the 0x80..0xFF half of each single-byte page (0x00..0x7F is ASCII); 0 where the page has no character
static const unsigned short cof_text_cp1250[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021, 0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

static const unsigned short cof_text_cp1251[128] =
{
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

static const unsigned short cof_text_cp1252[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

// A code page the text layer knows; anything else is read as 1252.
static inline int CoF_Text_Codepage( int codepage )
{
	return ( codepage == 1250 || codepage == 1251 || codepage == COF_CP_UTF8 ) ? codepage : 1252;
}

// One byte of a single-byte page as Unicode; 0 when the page has nothing there (and for 0 itself).
static inline uint32_t CoF_Text_Byte( int codepage, int b )
{
	const unsigned short *table;

	b &= 0xFF;
	if( b < 0x80 )
		return (uint32_t)b;

	switch( codepage )
	{
	case 1250: table = cof_text_cp1250; break;
	case 1251: table = cof_text_cp1251; break;
	default:   table = cof_text_cp1252; break;
	}

	return table[b - 0x80];
}

// Unicode as a cp1252 byte, for the engine's cp1252 atlases; 0 when cp1252 has no such character.
static inline int CoF_Text_To1252( uint32_t cp )
{
	int i;

	if( cp < 0x80 )
		return (int)cp;
	if( cp >= 0xA0 && cp <= 0xFF )
		return (int)cp; // cp1252 is Latin-1 from 0xA0 up

	for( i = 0; i < 32; i++ )
	{
		if( cof_text_cp1252[i] && cof_text_cp1252[i] == cp )
			return 0x80 + i;
	}

	return 0;
}

// UTF-8: the length a byte announces as the first of a character, 0 when it cannot start one (a continuation
// byte, an overlong lead 0xC0/0xC1, or 0xF5 and up).
static inline int CoF_Text_Utf8Len( int b )
{
	b &= 0xFF;
	if( b < 0x80 ) return 1;
	if( b >= 0xC2 && b <= 0xDF ) return 2;
	if( b >= 0xE0 && b <= 0xEF ) return 3;
	if( b >= 0xF0 && b <= 0xF4 ) return 4;
	return 0;
}

static inline int CoF_Text_Utf8Cont( int b )
{
	return ( b & 0xC0 ) == 0x80;
}

// A complete, well-formed multi-byte UTF-8 sequence at s (at most len bytes there): its length (2..4), the code
// point in *cp; 0 when there is none (ASCII, a stray byte, a cut or overlong sequence, a surrogate).
static inline int CoF_Text_Utf8Seq( const unsigned char *s, int len, uint32_t *cp )
{
	int n = CoF_Text_Utf8Len( s[0] ), i;
	uint32_t c;

	if( n < 2 || n > len )
		return 0;

	c = s[0] & ( n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07 );
	for( i = 1; i < n; i++ )
	{
		if( !CoF_Text_Utf8Cont( s[i] ))
			return 0;
		c = ( c << 6 ) | ( s[i] & 0x3F );
	}

	if(( n == 3 && c < 0x800 ) || ( n == 4 && ( c < 0x10000 || c > 0x10FFFF )) || ( c >= 0xD800 && c <= 0xDFFF ))
		return 0;

	*cp = c;
	return n;
}

// The next character of a string in `codepage` (tolerant: see the header). Returns the bytes it takes (1..4) and
// the code point in *cp; a byte UTF-8 text cannot place reads as U+FFFD; a byte a single-byte page has no
// character for reads as 0 (the caller draws nothing for it). len <= 0 returns 0.
static inline int CoF_Text_Next( const unsigned char *s, int len, int codepage, int tolerant, uint32_t *cp )
{
	int n;

	if( len <= 0 )
		return 0;

	codepage = CoF_Text_Codepage( codepage );

	if( s[0] < 0x80 )
	{
		*cp = s[0];
		return 1;
	}

	if( codepage == COF_CP_UTF8 || ( tolerant && codepage == 1252 ))
	{
		if(( n = CoF_Text_Utf8Seq( s, len, cp )) > 0 )
			return n;

		if( codepage == COF_CP_UTF8 )
		{
			*cp = 0xFFFD;
			return 1;
		}
	}

	*cp = CoF_Text_Byte( codepage, s[0] );
	return 1;
}

// A decoder fed one byte at a time, for the paths that only see one (client.dll's HUD messages through
// pfnDrawCharacter, VGUI's drawPrintChar). UTF-8 only: a single-byte page needs no state. Feed returns 1 with *cp
// set when a character is complete, 0 while a sequence is still open. A byte that breaks an open sequence abandons
// it and is read on its own.
typedef struct
{
	uint32_t	cp;
	int		need;	// continuation bytes still to come
	int		len;	// the whole sequence's length
} cof_text_stream_t;

static inline void CoF_Text_StreamReset( cof_text_stream_t *st )
{
	st->cp = 0;
	st->need = 0;
	st->len = 0;
}

static inline int CoF_Text_Feed( cof_text_stream_t *st, int b, uint32_t *cp )
{
	int n;

	b &= 0xFF;

	if( st->need > 0 && CoF_Text_Utf8Cont( b ))
	{
		st->cp = ( st->cp << 6 ) | (uint32_t)( b & 0x3F );

		if( --st->need > 0 )
			return 0;

		if(( st->len == 3 && st->cp < 0x800 ) || ( st->len == 4 && ( st->cp < 0x10000 || st->cp > 0x10FFFF )) ||
			( st->cp >= 0xD800 && st->cp <= 0xDFFF ))
			st->cp = 0xFFFD;

		*cp = st->cp;
		CoF_Text_StreamReset( st );
		return 1;
	}

	CoF_Text_StreamReset( st ); // whatever was open is abandoned

	if( b < 0x80 )
	{
		*cp = (uint32_t)b;
		return 1;
	}

	n = CoF_Text_Utf8Len( b );
	if( n < 2 )
	{
		*cp = 0xFFFD;
		return 1;
	}

	st->len = n;
	st->need = n - 1;
	st->cp = (uint32_t)( b & ( n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07 ));
	return 0;
}

// The cell a byte of UTF-8 text takes in client.dll's byte-by-byte layout, which sums a 256-entry width table:
// a lead byte the whole character's (2 a full cell, the width of a Chinese character; 1 half of one, for the two-byte
// characters: Latin-1, Greek, Cyrillic), a continuation byte and anything that cannot start a character nothing. The
// engine draws the character when its last byte arrives, at the place its lead byte was given.
static inline int CoF_Text_Utf8ByteCell( int b )
{
	switch( CoF_Text_Utf8Len( b ))
	{
	case 1: return -1; // ASCII: its own width, not a cell
	case 2: return 1;
	case 3:
	case 4: return 2;
	default: return 0;
	}
}

// Wide: a CJK character, its punctuation or a full-width form. Each one is a place a line may break, and a full
// cell in UTF-8 text.
static inline int CoF_Text_IsWide( uint32_t cp )
{
	return ( cp >= 0x1100 && cp <= 0x115F ) || ( cp >= 0x2E80 && cp <= 0x303E ) || ( cp >= 0x3040 && cp <= 0x33FF ) ||
		( cp >= 0x3400 && cp <= 0x4DBF ) || ( cp >= 0x4E00 && cp <= 0x9FFF ) || ( cp >= 0xA000 && cp <= 0xA4CF ) ||
		( cp >= 0xAC00 && cp <= 0xD7A3 ) || ( cp >= 0xF900 && cp <= 0xFAFF ) || ( cp >= 0xFE30 && cp <= 0xFE4F ) ||
		( cp >= 0xFF00 && cp <= 0xFF60 ) || ( cp >= 0xFFE0 && cp <= 0xFFE6 ) || ( cp >= 0x20000 && cp <= 0x3FFFD );
}

// Closing punctuation: never at the start of a line (。，、！？：；）」』】》〉… and their full-width forms).
static inline int CoF_Text_NoBreakBefore( uint32_t cp )
{
	switch( cp )
	{
	case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F: case 0xFF1A: case 0xFF1B:
	case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x300B: case 0x3009: case 0x3015: case 0x3017:
	case 0x2026: case 0x2014: case 0x201D: case 0x2019: case 0x30FC: case 0xFF5E: case 0x00B7: case 0x30FB:
	case ',': case '.': case '!': case '?': case ':': case ';': case ')': case ']': case '}': case '%':
		return 1;
	}
	return 0;
}

// Opening punctuation: never at the end of a line (（「『【《〈“‘).
static inline int CoF_Text_NoBreakAfter( uint32_t cp )
{
	switch( cp )
	{
	case 0xFF08: case 0x300C: case 0x300E: case 0x3010: case 0x300A: case 0x3008: case 0x3014: case 0x3016:
	case 0x201C: case 0x2018: case '(': case '[': case '{':
		return 1;
	}
	return 0;
}

// May a line break between two characters that touch (no space between them)? Only where one of them is wide, and
// not before closing or after opening punctuation. Spaces are breaks of their own and never reach here.
static inline int CoF_Text_BreakBetween( uint32_t prev, uint32_t next )
{
	if( !prev || !next )
		return 0;
	if( !CoF_Text_IsWide( prev ) && !CoF_Text_IsWide( next ))
		return 0;
	if( CoF_Text_NoBreakBefore( next ) || CoF_Text_NoBreakAfter( prev ))
		return 0;
	return 1;
}

// Which face draws a character beyond the Latin one: 1 the CJK face, 0 the Latin face (Inter). In UTF-8 text the
// general punctuation (… — “ ”) comes from the CJK face too, where it has the full-width shape Chinese text uses.
static inline int CoF_Text_PreferCJK( uint32_t cp, int utf8 )
{
	if( CoF_Text_IsWide( cp ))
		return 1;
	if( utf8 && cp >= 0x2010 && cp <= 0x206F )
		return 1;
	return 0;
}

#endif // COF_TEXT_LOGIC_H
