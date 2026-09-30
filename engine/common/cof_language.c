/*
cof_language.c - Cry of Fear native language packs (cof_language)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

====================================================================

A language pack is a folder under the game directory:

	<gamedir>/languages/<code>/
		manifest.txt                display_name, codepage, authors (key=value)
		txt/txtfiles/languages/<code>/*.txt
		txt/notes/languages/<code>/*.txt
		inventoryitems/**.txt
		maps/<map>.entpatch         translated entity key/values (cof_entpatch.c)
		maps/<map>.ent              whole entity-lump override (older packs; wins)
		textures/<texname>.tga      repainted world signs and posters
		models/<model>/<tex>.bmp    repainted textures embedded in studio models
		overlay/<gamedir>/**        same-path overlay (translated interface art)
		strings/dll-strings.tsv     english <TAB> localised <TAB> ... (UTF-8)

Three completely different mechanisms are needed, because Cry of Fear loads
its localisable data three different ways (measured in
stage1/polish-file-access-20260922/RESULTS.md):

 1. Assets the ENGINE loads (overlay art, per-map entity patches, repainted
    world and model textures) are reachable from the engine filesystem. The
    overlay folder is added to the search path at the highest priority after
    every rescan; entity patches and texture overrides are probed by path,
    because they do not live at their game-relative path inside the pack,
    and are applied to the game's own data in memory (entity string at map
    load, embedded model textures at model load).

 2. Text the GAME DLLs open themselves (txtfiles/, notes/, inventoryitems/)
    is invisible to the engine filesystem: client.dll and hl.dll are linked
    against the static MSVC runtime and every std::ifstream in them lands on
    one KERNEL32!CreateFileW import thunk, with a path built from the bare
    game directory and resolved by the CRT against the process current
    directory. The engine patches that one import entry per module, in
    memory, right after LoadLibrary, restores it before FreeLibrary, and
    redirects read-only opens under those three folders into the pack.
    Disk is untouched; English is never modified.

 3. Strings compiled into the DLLs' .rdata have no file slot at all, so they
    are substituted at DRAW time from the pack's table - at
    TextImage::setText for VGUI text and at CL_DrawString/CL_DrawStringLen
    (client-DLL calls only) and the centre print for the engine font path.

Everything here is language-generic. The only language-specific things are
the folder name (cof_language) and the manifest's code page, which is pushed
into cof_text_codepage so the Inter font layer decodes the pack's bytes.
====================================================================
*/

#include "common.h"
#include "library.h"
#include "platform/platform.h"
#include "cof_language.h"
#include "cof_entpatch.h"
#include "studio.h"
#include "utflib.h"

#if XASH_WIN32
#include <windows.h>
#endif

static CVAR_DEFINE_AUTO( cof_language, "", FCVAR_ARCHIVE, "active Cry of Fear language pack, a folder name under <gamedir>/languages; empty is English and turns every part of this off" );
static CVAR_DEFINE_AUTO( cof_language_strings, "1", FCVAR_ARCHIVE, "substitute the strings compiled into the game DLLs from the active pack's table at draw time" );
static CVAR_DEFINE_AUTO( cof_language_files, "1", FCVAR_ARCHIVE, "redirect the game DLLs' own txtfiles/, notes/ and inventoryitems/ reads into the active pack" );
static CVAR_DEFINE_AUTO( cof_language_trace, "0", 0, "developer: log every file the hooked game DLLs open through CreateFileW, redirected or not" );

#define COF_LANG_MAX_PARTS	8	// placeholders in one table entry, + 1 literal
#define COF_LANG_HASH		256
#define COF_LANG_MAX_STRING	1024
#define COF_LANG_MAX_HOOKS	4

typedef struct cof_lang_entry_s
{
	struct cof_lang_entry_s	*hash_next;	// exact-match chain
	struct cof_lang_entry_s	*fmt_next;	// format list, sorted by literal length
	char			*src;		// English, as the DLL holds it
	char			*dst;		// localised, in the pack's code page (VGUI)
	char			*dst_utf8;	// localised, UTF-8, folded for the engine font
	char			*dst_cp;	// localised, pack code page, folded for the engine font
	int			src_len;
	int			nparts;		// literal pieces of src; 1 == no placeholder
	int			literal_len;	// total length of those pieces
	char			*part[COF_LANG_MAX_PARTS + 1];
	int			part_len[COF_LANG_MAX_PARTS + 1];
} cof_lang_entry_t;

#if XASH_WIN32
typedef struct
{
	HMODULE		module;
	ULONG_PTR	*slot;		// the IAT entry we wrote
	ULONG_PTR	original;	// what it held before
	char		tag[64];
} cof_lang_hook_t;
#endif

static struct
{
	qboolean	initialized;
	qboolean	active;
	string		code;			// "polish", "" when off
	char		dir[MAX_QPATH];		// "languages/polish"
	char		display[64];
	char		authors[256];
	int		codepage;
	qboolean	codepage_pending;	// cof_text_codepage did not exist yet

	// string table
	poolhandle_t		mempool;
	int			num_entries;
	cof_lang_entry_t	*hash[COF_LANG_HASH];
	cof_lang_entry_t	*fmt_head;
	int			num_fmt;
	int			num_hits;

#if XASH_WIN32
	// everything the CreateFileW shim reads is guarded by this lock: the shim
	// takes it shared, CoF_Lang_Apply takes it exclusive while it switches
	// packs. The shim may run on any thread the game DLLs open files from.
	SRWLOCK		lock;
	DWORD		main_thread;
	wchar_t		wcode[64];		// L"polish"
	wchar_t		wgame[MAX_QPATH];	// L"cryoffear"

	cof_lang_hook_t	hooks[COF_LANG_MAX_HOOKS];
	volatile LONG	redirects;
	volatile LONG	passthrough;		// a pack-class open the pack had no file for
	volatile LONG	offthread;		// redirects not logged: not the main thread
#endif
} cof_lang;

// the three code pages a pack may declare. Identical to the tables the font
// layer decodes with (3rdparty/freevgui/platform/xash3d-fwgs/coffont.cpp);
// there they map byte -> Unicode, here they are inverted at load time so the
// pack's UTF-8 text becomes the bytes the 8-bit pipeline wants.
static const unsigned short cof_cp1250[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

static const unsigned short cof_cp1251[128] =
{
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

static const unsigned short cof_cp1252[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

// Latin Extended-A (U+0100..U+017F) folded to its base letter. The engine's
// own fonts (console, HUD text atlases) are cp1252 or cp1251 sheets and have
// no cell for most of these, so the engine-font variant of a pack string
// reads "zolw" rather than "?o?w".
static const char cof_fold_latin_ext_a[128 + 1] =
	"AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
	"lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";

/*
====================
CoF_Lang_Note

The handful of once-per-session lines (hooks, activation, mount) go straight
to Sys_Print: Con_Printf drops everything while host.allow_console is still
false, which is exactly when the game DLLs are loaded and the pack applied.
====================
*/
static void CoF_Lang_Note( const char *fmt, ... ) FORMAT_CHECK( 1 );
static void CoF_Lang_Note( const char *fmt, ... )
{
	char buf[1024];
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, args );
	va_end( args );

	Sys_Print( buf );
}

qboolean CoF_Lang_Active( void )
{
	return cof_lang.active;
}

const char *CoF_Lang_Code( void )
{
	return cof_lang.code;
}

/*
====================
CoF_Lang_EncodeByte

One Unicode code point to one byte of the given code page. Returns 0 when
the page has no cell for it, which the caller turns into '?'.
====================
*/
static int CoF_Lang_EncodeByte( uint32_t uc, int codepage )
{
	const unsigned short *table;
	int i;

	if( uc < 0x80 )
		return (int)uc;

	switch( codepage )
	{
	case 1250: table = cof_cp1250; break;
	case 1251: table = cof_cp1251; break;
	default:   table = cof_cp1252; break;
	}

	for( i = 0; i < 128; i++ )
	{
		if( table[i] == uc )
			return 0x80 + i;
	}

	return 0;
}

/*
====================
CoF_Lang_Fold

What the engine's own fonts get: letters with no cell in a cp1252/cp1251
sheet become their base letter, typographic punctuation becomes ASCII.
Latin-1 and Cyrillic are left alone; the console maps those itself.
====================
*/
static uint32_t CoF_Lang_Fold( uint32_t uc )
{
	if( uc >= 0x100 && uc <= 0x17F )
		return (byte)cof_fold_latin_ext_a[uc - 0x100];

	switch( uc )
	{
	case 0x2018: case 0x2019: case 0x201A: return '\'';
	case 0x201C: case 0x201D: case 0x201E: return '"';
	case 0x2013: case 0x2014: return '-';
	case 0x2026: return '.';
	case 0x00A0: return ' ';
	}

	return uc;
}

typedef enum
{
	COF_ENC_CODEPAGE = 0,	// the pack's code page, verbatim
	COF_ENC_CODEPAGE_FOLDED,	// the pack's code page, folded for the engine font
	COF_ENC_UTF8_FOLDED,	// UTF-8, folded for the engine font
} cof_lang_enc_t;

/*
====================
CoF_Lang_Convert

The pack's TSV is UTF-8; the whole Cry of Fear text pipeline is one byte per
glyph. Convert once at load so the client's byte pipeline and the font
layer's code-page decode agree exactly.

Also unescapes the four C escapes the pack generator writes for control
characters it cannot put in a TSV cell (\n \t \r \\). Everything else stays
literal, so a Windows path like models\costumes survives untouched.
====================
*/
static void CoF_Lang_Convert( char *out, size_t size, const char *in, int codepage, cof_lang_enc_t enc )
{
	utfstate_t state;
	size_t o = 0;

	memset( &state, 0, sizeof( state ));

	while( *in && o + 1 < size )
	{
		uint32_t uc;

		if( in[0] == '\\' && in[1] )
		{
			char esc = 0;

			switch( in[1] )
			{
			case 'n':  esc = '\n'; break;
			case 't':  esc = '\t'; break;
			case 'r':  esc = '\r'; break;
			case '\\': esc = '\\'; break;
			}

			if( esc )
			{
				out[o++] = esc;
				in += 2;
				continue;
			}
		}

		uc = Q_DecodeUTF8( &state, (uint32_t)(byte)*in++ );
		if( !uc )
			continue; // incomplete sequence, feed more

		if( enc != COF_ENC_CODEPAGE )
			uc = CoF_Lang_Fold( uc );

		if( enc == COF_ENC_UTF8_FOLDED )
		{
			char tmp[4];
			size_t n = Q_EncodeUTF8( tmp, uc );

			if( !n || o + n + 1 > size )
				break;

			memcpy( out + o, tmp, n );
			o += n;
		}
		else
		{
			int ch = CoF_Lang_EncodeByte( uc, codepage );
			out[o++] = ch ? (char)ch : '?';
		}
	}

	out[o] = '\0';
}

/*
====================
CoF_Lang_HashKey
====================
*/
static uint CoF_Lang_HashKey( const char *s )
{
	uint h = 5381;

	while( *s )
		h = (( h << 5 ) + h ) ^ (byte)*s++;

	return h & ( COF_LANG_HASH - 1 );
}

/*
====================
CoF_Lang_IsPlaceholder

printf conversions as the game's strings use them: %s, %i, %d, %u, %f, %c,
%x, optionally with flags, a width or a precision. "%%" is a literal
percent and is not a placeholder. Returns the length of the conversion, or 0.
====================
*/
static int CoF_Lang_IsPlaceholder( const char *s )
{
	int n = 1;

	if( s[0] != '%' || s[1] == '%' )
		return 0;

	while( s[n] && (( s[n] >= '0' && s[n] <= '9' ) || s[n] == '.' || s[n] == '-' || s[n] == '+' ))
		n++;

	if( s[n] && Q_strchr( "sidufcxX", s[n] ))
		return n + 1;

	return 0;
}

/*
====================
CoF_Lang_SplitFormat

Split an entry's English text at its placeholders. nparts == 1 means the
entry is an exact-match one. Returns false for an entry that cannot be used
as a format (too many placeholders, two adjacent ones, or not enough literal
text to be distinctive).
====================
*/
static qboolean CoF_Lang_SplitFormat( cof_lang_entry_t *e )
{
	const char *s = e->src;
	const char *start = s;
	int i;

	e->nparts = 0;
	e->literal_len = 0;

	while( *s )
	{
		int n = CoF_Lang_IsPlaceholder( s );

		if( !n )
		{
			s++;
			continue;
		}

		if( e->nparts >= COF_LANG_MAX_PARTS )
			return false;

		e->part_len[e->nparts] = (int)( s - start );
		e->part[e->nparts] = Mem_Calloc( cof_lang.mempool, e->part_len[e->nparts] + 1 );
		memcpy( e->part[e->nparts], start, e->part_len[e->nparts] );
		e->nparts++;

		s += n;
		start = s;
	}

	if( !e->nparts )
	{
		e->nparts = 1; // exact match only
		return true;
	}

	// the trailing literal, possibly empty
	e->part_len[e->nparts] = (int)( s - start );
	e->part[e->nparts] = Mem_Calloc( cof_lang.mempool, e->part_len[e->nparts] + 1 );
	memcpy( e->part[e->nparts], start, e->part_len[e->nparts] );
	e->nparts++;

	for( i = 0; i < e->nparts; i++ )
	{
		// two adjacent placeholders cannot be matched unambiguously
		if( i > 0 && i < e->nparts - 1 && !e->part_len[i] )
			return false;

		e->literal_len += e->part_len[i];
	}

	// "%s" alone would match every string ever drawn
	if( e->literal_len < 3 )
		return false;

	return true;
}

/*
====================
CoF_Lang_FreeStrings
====================
*/
static void CoF_Lang_FreeStrings( void )
{
	if( cof_lang.mempool )
		Mem_FreePool( &cof_lang.mempool );

	cof_lang.mempool = 0;
	memset( cof_lang.hash, 0, sizeof( cof_lang.hash ));
	cof_lang.fmt_head = NULL;
	cof_lang.num_entries = 0;
	cof_lang.num_fmt = 0;
	cof_lang.num_hits = 0;
}

static char *CoF_Lang_CopyString( const char *s )
{
	size_t len = Q_strlen( s );
	char *p = Mem_Calloc( cof_lang.mempool, len + 1 );

	memcpy( p, s, len );
	return p;
}

/*
====================
CoF_Lang_AddEntry
====================
*/
static void CoF_Lang_AddEntry( const char *english, const char *localised )
{
	cof_lang_entry_t *e, **prev;
	char converted[COF_LANG_MAX_STRING];
	char plain[COF_LANG_MAX_STRING];
	char folded[COF_LANG_MAX_STRING];
	uint key;

	if( COM_StringEmptyOrNULL( english ) || COM_StringEmptyOrNULL( localised ))
		return;

	// the English side is 7-bit in every shipped pack, but it may carry the
	// same control-byte prefixes the DLL strings do, so run it through the
	// same unescaper. It is never folded: it must match the DLL bytes.
	CoF_Lang_Convert( plain, sizeof( plain ), english, 1252, COF_ENC_CODEPAGE );
	CoF_Lang_Convert( converted, sizeof( converted ), localised, cof_lang.codepage, COF_ENC_CODEPAGE );

	if( !Q_strcmp( plain, converted ))
		return; // kept in English by the pack author, nothing to do

	key = CoF_Lang_HashKey( plain );

	for( e = cof_lang.hash[key]; e; e = e->hash_next )
	{
		if( !Q_strcmp( e->src, plain ))
			return; // first row wins, later duplicates are ignored
	}

	e = Mem_Calloc( cof_lang.mempool, sizeof( *e ));
	e->src_len = Q_strlen( plain );
	e->src = CoF_Lang_CopyString( plain );
	e->dst = CoF_Lang_CopyString( converted );

	CoF_Lang_Convert( folded, sizeof( folded ), localised, cof_lang.codepage, COF_ENC_UTF8_FOLDED );
	e->dst_utf8 = CoF_Lang_CopyString( folded );
	CoF_Lang_Convert( folded, sizeof( folded ), localised, cof_lang.codepage, COF_ENC_CODEPAGE_FOLDED );
	e->dst_cp = CoF_Lang_CopyString( folded );

	e->hash_next = cof_lang.hash[key];
	cof_lang.hash[key] = e;
	cof_lang.num_entries++;

	if( !CoF_Lang_SplitFormat( e ) || e->nparts < 2 )
		return; // exact-match only

	// keep the format list sorted by descending literal length, so
	// "You got the %s" is tried before "You got %s" and wins
	for( prev = &cof_lang.fmt_head; *prev; prev = &( *prev )->fmt_next )
	{
		if(( *prev )->literal_len < e->literal_len )
			break;
	}

	e->fmt_next = *prev;
	*prev = e;
	cof_lang.num_fmt++;
}

/*
====================
CoF_Lang_LoadStrings

<pack>/strings/dll-strings.tsv: a header row, then
english <TAB> localised <TAB> source_dll <TAB> file_offset <TAB> flagged
====================
*/
static void CoF_Lang_LoadStrings( void )
{
	char path[MAX_QPATH];
	fs_offset_t len = 0;
	byte *raw;
	char *p, *end;
	qboolean first = true;

	CoF_Lang_FreeStrings();

	if( !cof_lang.active )
		return;

	cof_lang.mempool = Mem_AllocPool( "CoF language pack" );

	Q_snprintf( path, sizeof( path ), "%s/strings/dll-strings.tsv", cof_lang.dir );
	raw = FS_LoadFile( path, &len, true );

	if( !raw )
	{
		CoF_Lang_Note( S_WARN "cof_language: %s not found, DLL strings stay in English\n", path );
		return;
	}

	p = (char *)raw;
	end = p + len;

	while( p < end )
	{
		char *line = p;
		char *tab1, *tab2;
		size_t l;

		while( p < end && *p != '\n' )
			p++;

		*p = '\0'; // FS_LoadFile leaves one terminator byte past the end
		if( p < end )
			p++;

		l = Q_strlen( line );
		if( l && line[l - 1] == '\r' )
			line[l - 1] = '\0';

		if( first )
		{
			first = false;

			// a UTF-8 byte order mark, if an editor added one
			if( (byte)line[0] == 0xEF && (byte)line[1] == 0xBB && (byte)line[2] == 0xBF )
				line += 3;

			if( !Q_strncmp( line, "english\t", 8 ))
				continue; // header
		}

		if( !line[0] )
			continue;

		tab1 = Q_strchr( line, '\t' );
		if( !tab1 )
			continue;

		*tab1++ = '\0';
		tab2 = Q_strchr( tab1, '\t' );
		if( tab2 )
			*tab2 = '\0';

		CoF_Lang_AddEntry( line, tab1 );
	}

	Mem_Free( raw );

	CoF_Lang_Note( "cof_language: %i DLL strings (%i with placeholders) from %s\n", cof_lang.num_entries, cof_lang.num_fmt, path );
}

/*
====================
CoF_Lang_Rebuild

Drop the captured argument text into the template's placeholders, left to
right. A template with fewer placeholders than captures drops the rest; one
with more drops its surplus placeholders rather than drawing "%s".
====================
*/
static const char *CoF_Lang_Rebuild( const char *tmpl, const char **cap, const int *caplen, int ncap )
{
	static char	result[4][COF_LANG_MAX_STRING];
	static int	which = 0;
	const char *d = tmpl;
	char *out;
	size_t o = 0, room;
	int used = 0;

	which = ( which + 1 ) & 3;
	out = result[which];
	room = sizeof( result[0] );

	while( *d && o + 1 < room )
	{
		int n = CoF_Lang_IsPlaceholder( d );

		if( n )
		{
			if( used < ncap )
			{
				size_t c = caplen[used];

				if( o + c + 1 >= room )
					c = room - o - 1;

				memcpy( out + o, cap[used], c );
				o += c;
			}

			used++;
			d += n;
			continue;
		}

		out[o++] = *d++;
	}

	out[o] = '\0';
	return out;
}

/*
====================
CoF_Lang_Lookup

Exact match first, then the placeholder-aware formats: the entry's literal
pieces must appear in order, the first one at the start and the last one at
the end of the string, and what sits between them is captured.
====================
*/
static const char *CoF_Lang_Lookup( const char *str, cof_lang_enc_t enc )
{
	cof_lang_entry_t *e;
	uint key;
	int len;

	if( !cof_lang.active || !cof_language_strings.value || !cof_lang.mempool )
		return str;

	if( COM_StringEmptyOrNULL( str ))
		return str;

	len = Q_strlen( str );
	if( len >= COF_LANG_MAX_STRING )
		return str;

	key = CoF_Lang_HashKey( str );

	for( e = cof_lang.hash[key]; e; e = e->hash_next )
	{
		if( e->src_len == len && !Q_strcmp( e->src, str ))
		{
			cof_lang.num_hits++;
			switch( enc )
			{
			case COF_ENC_UTF8_FOLDED: return e->dst_utf8;
			case COF_ENC_CODEPAGE_FOLDED: return e->dst_cp;
			default: return e->dst;
			}
		}
	}

	for( e = cof_lang.fmt_head; e; e = e->fmt_next )
	{
		const char *cap[COF_LANG_MAX_PARTS];
		int caplen[COF_LANG_MAX_PARTS];
		const char *s = str;
		const char *tail;
		int i, ncap = 0;
		qboolean ok = true;

		if( e->literal_len > len )
			continue;

		if( e->part_len[0] )
		{
			if( Q_strncmp( s, e->part[0], e->part_len[0] ))
				continue;
			s += e->part_len[0];
		}

		// everything between the first and the last literal piece
		for( i = 1; i < e->nparts - 1; i++ )
		{
			const char *found = Q_strstr( s, e->part[i] );

			if( !found )
			{
				ok = false;
				break;
			}

			cap[ncap] = s;
			caplen[ncap] = (int)( found - s );
			ncap++;
			s = found + e->part_len[i];
		}

		if( !ok )
			continue;

		// the trailing literal piece has to close the string
		tail = str + len - e->part_len[e->nparts - 1];

		if( tail < s )
			continue;

		if( e->part_len[e->nparts - 1] && Q_strncmp( tail, e->part[e->nparts - 1], e->part_len[e->nparts - 1] ))
			continue;

		cap[ncap] = s;
		caplen[ncap] = (int)( tail - s );
		ncap++;

		cof_lang.num_hits++;

		switch( enc )
		{
		case COF_ENC_UTF8_FOLDED: return CoF_Lang_Rebuild( e->dst_utf8, cap, caplen, ncap );
		case COF_ENC_CODEPAGE_FOLDED: return CoF_Lang_Rebuild( e->dst_cp, cap, caplen, ncap );
		default: return CoF_Lang_Rebuild( e->dst, cap, caplen, ncap );
		}
	}

	return str;
}

const char *CoF_Lang_String( const char *str )
{
	return CoF_Lang_Lookup( str, COF_ENC_CODEPAGE );
}

const char *CoF_Lang_StringEngineFont( const char *str, qboolean utf8 )
{
	return CoF_Lang_Lookup( str, utf8 ? COF_ENC_UTF8_FOLDED : COF_ENC_CODEPAGE_FOLDED );
}

/*
====================
CoF_Lang_EntPatch

maps/<map>.ent from the pack, when it carries one. The engine's "the patch
must be newer than the .bsp" rule is not applied to it: the pack is data we
install, and a copy that did not preserve timestamps would otherwise
silently disable a whole map's translation.
====================
*/
qboolean CoF_Lang_EntPatch( const char *entfilename, char *out, size_t size )
{
	if( !cof_lang.active || COM_StringEmptyOrNULL( entfilename ))
		return false;

	if( Q_snprintf( out, size, "%s/maps/%s", cof_lang.dir, COM_FileWithoutPath( entfilename )) < 0 )
		return false;

	if( !FS_FileExists( out, true ))
		return false;

	if( FS_FileExists( entfilename, true ))
		Con_Printf( S_WARN "cof_language: %s replaces the game directory's own %s\n", out, entfilename );

	Con_DPrintf( "[cof-lang] entity patch %s -> %s\n", entfilename, out );
	return true;
}

/*
====================
CoF_Lang_ApplyEntityPatch

maps/<map>.entpatch: the translated key/value pairs of one map, applied to
the entity string the map loads anyway (BSP lump or gamedir .ent), after the
normal .ent override logic. A pack that still carries a full maps/<map>.ent
uses that instead (CoF_Lang_EntPatch) and its .entpatch is ignored.
====================
*/
static void CoF_Lang_EntPatchLog( void *ctx, const char *msg )
{
	Con_Printf( S_WARN "cof_language: %s: %s\n", (const char *)ctx, msg );
}

char *CoF_Lang_ApplyEntityPatch( const char *entfilename, const char *entdata, size_t entdatasize, size_t *outsize )
{
	char ent[MAX_SYSPATH], path[MAX_SYSPATH];
	cof_entpatch_stats_t st;
	fs_offset_t patchsize;
	byte *patch;
	char *res, *out;
	size_t len;

	if( !cof_lang.active || COM_StringEmptyOrNULL( entfilename ) || !entdata || !outsize )
		return NULL;

	if( Q_snprintf( ent, sizeof( ent ), "%s/maps/%s", cof_lang.dir, COM_FileWithoutPath( entfilename )) < 0 )
		return NULL;

	if( FS_FileExists( ent, true ))
		return NULL; // the pack's full .ent already replaced the string

	Q_strncpy( path, ent, sizeof( path ));
	COM_ReplaceExtension( path, ".entpatch", sizeof( path ));

	if( !FS_FileExists( path, true ))
		return NULL;

	if(( patch = FS_LoadFile( path, &patchsize, true )) == NULL )
		return NULL;

	res = CoF_EntPatch_Apply( entdata, entdatasize, (const char *)patch, (size_t)patchsize, &len, &st, CoF_Lang_EntPatchLog, path );
	Mem_Free( patch );

	if( !res )
	{
		Con_Printf( S_WARN "cof_language: %s not applied\n", path );
		return NULL;
	}

	out = Mem_Malloc( host.mempool, len + 1 );
	memcpy( out, res, len + 1 );
	free( res );

	Con_Printf( "^2Applied entity patch:^7 %s (%d value(s), %d removed, %d added, %d skipped)\n",
		path, st.edited, st.removed, st.added, st.skipped );

	*outsize = len;
	return out;
}

/*
====================
CoF_Lang_StudioTextures

Texture overrides for studio models. The translated signs, newspapers, phone
screens and book pages of Cry of Fear are textures EMBEDDED in .mdl files; a
pack carries only the repainted textures, as

	languages/<code>/models/<model path without .mdl>/<texture name>

(8-bit indexed BMP, the texture's own width and height, up to 256 colours;
the texture name keeps its own extension: ".../v_mobile/tex1.bmp"). They are
written into the engine's private copy of the model right after it is read
and before the renderer uploads the textures, so every consumer - the
engine's renderer and the game client's own studio renderer - sees the pack's
pixels. The model file, its CRC and everything but those texels stay as they
are. A BMP of the wrong size or format is refused and logged.
====================
*/
static int CoF_Lang_Le32( const byte *p )
{
	return (int)( p[0] | p[1] << 8 | p[2] << 16 | (uint)p[3] << 24 );
}

static qboolean CoF_Lang_ReadBMP8( const byte *buf, size_t size, int width, int height, byte *pixels, byte *palette )
{
	int offbits, hdrsize, w, h, bpp, compression, colors, rowsize, y;
	qboolean topdown;

	if( size < 54 || buf[0] != 'B' || buf[1] != 'M' )
		return false;

	offbits = CoF_Lang_Le32( buf + 10 );
	hdrsize = CoF_Lang_Le32( buf + 14 );
	w = CoF_Lang_Le32( buf + 18 );
	h = CoF_Lang_Le32( buf + 22 );
	bpp = buf[28] | buf[29] << 8;
	compression = CoF_Lang_Le32( buf + 30 );
	colors = CoF_Lang_Le32( buf + 46 );
	topdown = h < 0;
	if( topdown ) h = -h;

	if( hdrsize < 40 || bpp != 8 || compression != 0 || w != width || h != height )
		return false;
	if( colors <= 0 || colors > 256 )
		colors = 256;

	rowsize = ( w + 3 ) & ~3;
	if( offbits < 0 || (size_t)offbits + (size_t)rowsize * h > size )
		return false;
	if( 14 + (size_t)hdrsize + (size_t)colors * 4 > size )
		return false;

	memset( palette, 0, 768 );
	for( y = 0; y < colors; y++ )
	{
		const byte *q = buf + 14 + hdrsize + y * 4; // B G R x
		palette[y * 3 + 0] = q[2];
		palette[y * 3 + 1] = q[1];
		palette[y * 3 + 2] = q[0];
	}

	for( y = 0; y < h; y++ )
	{
		const byte *row = buf + offbits + (size_t)( topdown ? y : h - 1 - y ) * rowsize;
		memcpy( pixels + (size_t)y * w, row, w );
	}

	return true;
}

void CoF_Lang_StudioTextures( const char *modelname, void *data, size_t size )
{
	studiohdr_t *phdr = data;
	mstudiotexture_t *ptex;
	char base[MAX_SYSPATH], path[MAX_SYSPATH]; // pack paths outgrow MAX_QPATH
	int i, replaced = 0;

	if( !cof_lang.active || !phdr || COM_StringEmptyOrNULL( modelname ))
		return;

	if( size < sizeof( studiohdr_t ) || phdr->numtextures <= 0 || phdr->textureindex <= 0 )
		return;

	if( (size_t)phdr->textureindex + (size_t)phdr->numtextures * sizeof( mstudiotexture_t ) > size )
		return;

	// "models/weapons/mobile/v_mobile.mdl" -> "models/weapons/mobile/v_mobile"
	Q_strncpy( base, modelname, sizeof( base ));
	COM_FixSlashes( base );
	COM_StripExtension( base );
	if( Q_strnicmp( base, "models/", 7 ))
		return;

	ptex = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex );

	for( i = 0; i < phdr->numtextures; i++, ptex++ )
	{
		size_t npix = (size_t)ptex->width * ptex->height;
		fs_offset_t filesize;
		byte *file, *pixels, palette[768];
		char texname[sizeof( ptex->name ) + 1];

		if( ptex->width <= 0 || ptex->height <= 0 || ptex->index <= 0 )
			continue;
		if( (size_t)ptex->index + npix + 768 > size )
			continue; // this buffer does not hold the texels

		Q_strncpy( texname, ptex->name, sizeof( texname ));
		if( Q_snprintf( path, sizeof( path ), "%s/%s/%s%s", cof_lang.dir, base, texname,
			Q_stristr( texname, ".bmp" ) ? "" : ".bmp" ) < 0 )
			continue;

		if( !FS_FileExists( path, true ))
			continue;

		if(( file = FS_LoadFile( path, &filesize, true )) == NULL )
			continue;

		pixels = Mem_Malloc( host.mempool, npix );
		if( CoF_Lang_ReadBMP8( file, (size_t)filesize, ptex->width, ptex->height, pixels, palette ))
		{
			memcpy( (byte *)phdr + ptex->index, pixels, npix );
			memcpy( (byte *)phdr + ptex->index + npix, palette, sizeof( palette ));
			replaced++;
			Con_DPrintf( "[cof-lang] model texture %s:%s -> %s\n", modelname, texname, path );
		}
		else Con_Printf( S_WARN "cof_language: %s is not an 8-bit %dx%d BMP; %s:%s left as it is\n",
			path, ptex->width, ptex->height, modelname, texname );

		Mem_Free( pixels );
		Mem_Free( file );
	}

	if( replaced )
		Con_Printf( "^2Model textures from the language pack:^7 %s (%d)\n", modelname, replaced );
}

/*
====================
CoF_Lang_TextureReplacement

The pack's repainted signs and posters. These are a language override, not
an "HD materials" opt-in, so they are probed before the stock materials/
paths and without host_allow_materials.
====================
*/
qboolean CoF_Lang_TextureReplacement( char *out, size_t size, const char *texname, const char *type )
{
	if( !cof_lang.active || COM_StringEmptyOrNULL( texname ))
		return false;

	if( Q_snprintf( out, size, "%s/textures/%s%s.tga", cof_lang.dir, texname, type ) < 0 )
		return false;

	if( !FS_FileExists( out, false ))
		return false;

	Con_DPrintf( "[cof-lang] texture override %s%s -> %s\n", texname, type, out );
	return true;
}

/*
====================
CoF_Lang_GameFile

The same redirect as the CreateFileW shim below, for the game DLLs' reads
through the ENGINE filesystem (pfnLoadFileForMe / COM_LoadFile): some of
their text (hints.txt, for one) is loaded that way rather than with the CRT.

  txtfiles/[languages/<slot>/]<rest>  -> languages/<code>/txt/txtfiles/languages/<code>/<rest>
                                       -> languages/<code>/txt/txtfiles/<rest>
  notes/...                           -> the same under txt/notes/
  inventoryitems/<rest>               -> languages/<code>/inventoryitems/<rest>

`name` is gamedir-relative with forward slashes. Returns true and fills
`out` only when the pack has the file.
====================
*/
qboolean CoF_Lang_GameFile( const char *name, char *out, size_t size )
{
	static const struct { const char *folder; qboolean slot; } classes[] =
	{
		{ "txtfiles/", true }, { "notes/", true }, { "inventoryitems/", false },
	};
	const char *rest = NULL;
	int cls, c;

	if( cof_language_trace.value && !COM_StringEmptyOrNULL( name ))
		CoF_Lang_Note( "[cof-lang] trace: LoadFileForMe %s\n", name );

	if( !cof_lang.active || !cof_language_files.value || COM_StringEmptyOrNULL( name ))
		return false;

	if( name[0] == '.' && ( name[1] == '/' || name[1] == '\\' ))
		name += 2;

	for( cls = 0; cls < (int)ARRAYSIZE( classes ); cls++ )
	{
		if( !Q_strnicmp( name, classes[cls].folder, Q_strlen( classes[cls].folder )))
		{
			rest = name + Q_strlen( classes[cls].folder );
			break;
		}
	}

	if( !rest )
		return false;

	if( classes[cls].slot && !Q_strnicmp( rest, "languages/", 10 ))
	{
		const char *slash = Q_strchr( rest + 10, '/' );

		if( slash )
			rest = slash + 1;
	}

	if( !*rest || Q_strstr( rest, ".." ))
		return false;

	for( c = 0; c < ( classes[cls].slot ? 2 : 1 ); c++ )
	{
		int n;

		if( !classes[cls].slot )
			n = Q_snprintf( out, size, "%s/%s%s", cof_lang.dir, classes[cls].folder, rest );
		else if( c == 0 )
			n = Q_snprintf( out, size, "%s/txt/%slanguages/%s/%s", cof_lang.dir, classes[cls].folder, cof_lang.code, rest );
		else
			n = Q_snprintf( out, size, "%s/txt/%s%s", cof_lang.dir, classes[cls].folder, rest );

		if( n < 0 || (size_t)n >= size )
			continue;

		if( FS_FileExists( out, true ))
		{
			Con_DPrintf( "[cof-lang] load %s -> %s\n", name, out );
			return true;
		}
	}

	return false;
}

/*
====================
The CreateFileW import shim
====================
*/
#if XASH_WIN32
typedef struct
{
	const char	*folder;	// the game-relative folder the DLL opens
	qboolean	has_language_slot;
} cof_lang_class_t;

static const cof_lang_class_t cof_lang_classes[] =
{
	{ "txtfiles/",       true  },
	{ "notes/",          true  },
	{ "inventoryitems/", false },
};

static wchar_t CoF_Lang_WLower( wchar_t c )
{
	if( c == L'\\' )
		return L'/';
	if( c >= L'A' && c <= L'Z' )
		return (wchar_t)( c - L'A' + L'a' );
	return c;
}

// case- and slash-insensitive prefix test against an ASCII string
static qboolean CoF_Lang_WStartsWithA( const wchar_t *s, const char *ascii )
{
	for( ; *ascii; s++, ascii++ )
	{
		if( CoF_Lang_WLower( *s ) != (wchar_t)CoF_Lang_WLower( (wchar_t)(byte)*ascii ))
			return false;
	}

	return true;
}

// the same against a wide string, followed by a separator
static size_t CoF_Lang_WSegment( const wchar_t *s, const wchar_t *seg )
{
	size_t n = 0;

	for( ; seg[n]; n++ )
	{
		if( CoF_Lang_WLower( s[n] ) != CoF_Lang_WLower( seg[n] ))
			return 0;
	}

	return CoF_Lang_WLower( s[n] ) == L'/' ? n + 1 : 0;
}

static qboolean CoF_Lang_WAppend( wchar_t *dst, size_t size, size_t *pos, const wchar_t *src, size_t len )
{
	if( *pos + len + 1 > size )
		return false;

	memcpy( dst + *pos, src, len * sizeof( wchar_t ));
	*pos += len;
	dst[*pos] = L'\0';
	return true;
}

static qboolean CoF_Lang_WAppendA( wchar_t *dst, size_t size, size_t *pos, const char *src )
{
	for( ; *src; src++ )
	{
		if( *pos + 2 > size )
			return false;
		dst[( *pos )++] = (wchar_t)(byte)*src;
	}

	dst[*pos] = L'\0';
	return true;
}

static qboolean CoF_Lang_WIsFile( const wchar_t *path )
{
	DWORD attr = GetFileAttributesW( path );

	return attr != INVALID_FILE_ATTRIBUTES && !FBitSet( attr, FILE_ATTRIBUTE_DIRECTORY );
}

/*
====================
CoF_Lang_RedirectPathW

  <prefix>cryoffear/txtfiles/[languages/<slot>/]<rest>
      -> <prefix>cryoffear/languages/<code>/txt/txtfiles/languages/<code>/<rest>
      -> <prefix>cryoffear/languages/<code>/txt/txtfiles/<rest>
  (the same for notes/), and
  <prefix>cryoffear/inventoryitems/<rest>
      -> <prefix>cryoffear/languages/<code>/inventoryitems/<rest>

The folder has to sit directly under the game directory segment (or open
the path, which is how the server names hints.txt), so a "notes" folder
somewhere in the install path can never match. The client's own
languages/<slot>/ segment for the stock languages 2..7 is dropped: a pack's
text always lives under its own languages/<code>/ folder whatever slot the
client thinks it is using.

Only read-only OPEN_EXISTING opens are ever touched, and only when the pack
really has the file. Everything else falls through unchanged, which is what
keeps SAVE\, config.cfg, scriptsettings.dat and maps\*.cfg writes out of it.

Runs with cof_lang.lock held shared.
====================
*/
static qboolean CoF_Lang_RedirectPathW( const wchar_t *name, DWORD access, DWORD disposition, wchar_t *out, size_t size, qboolean *class_hit )
{
	size_t i, n, pos, prefix_len = 0, folder_at = (size_t)-1;
	const wchar_t *rest;
	int cls = -1, c;

	*class_hit = false;

	if( !cof_lang.active || !cof_language_files.value )
		return false;

	if( !name || !name[0] )
		return false;

	if( FBitSet( access, GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA ))
		return false;

	if( disposition != OPEN_EXISTING )
		return false;

	n = wcslen( name );
	if( n < 6 || n > 512 )
		return false;

	// the class folder right after a "<gamedir>/" segment
	for( i = 0; i < n && cls < 0; i++ )
	{
		size_t seg;

		if( i > 0 && name[i - 1] != L'/' && name[i - 1] != L'\\' && name[i - 1] != L':' )
			continue;

		seg = CoF_Lang_WSegment( &name[i], cof_lang.wgame );
		if( !seg )
			continue;

		for( c = 0; c < (int)ARRAYSIZE( cof_lang_classes ); c++ )
		{
			if( CoF_Lang_WStartsWithA( &name[i + seg], cof_lang_classes[c].folder ))
			{
				cls = c;
				folder_at = i + seg;
				prefix_len = i + seg;
				break;
			}
		}
	}

	// or the class folder opening a bare relative path
	for( c = 0; cls < 0 && c < (int)ARRAYSIZE( cof_lang_classes ); c++ )
	{
		if( CoF_Lang_WStartsWithA( name, cof_lang_classes[c].folder ))
		{
			cls = c;
			folder_at = 0;
			prefix_len = 0;
		}
	}

	if( cls < 0 )
		return false;

	*class_hit = true;
	rest = &name[folder_at + Q_strlen( cof_lang_classes[cls].folder )];

	// the client's own languages/<slot>/ segment, if any
	if( cof_lang_classes[cls].has_language_slot && CoF_Lang_WStartsWithA( rest, "languages/" ))
	{
		const wchar_t *slash = rest + 10;

		while( *slash && *slash != L'/' && *slash != L'\\' )
			slash++;

		if( *slash )
			rest = slash + 1;
	}

	if( !*rest )
		return false;

	// two candidates for the slotted classes, one for inventoryitems
	for( c = 0; c < ( cof_lang_classes[cls].has_language_slot ? 2 : 1 ); c++ )
	{
		pos = 0;
		out[0] = L'\0';

		if( prefix_len )
		{
			if( !CoF_Lang_WAppend( out, size, &pos, name, prefix_len ))
				return false;
		}
		else
		{
			// a bare path: put the game directory back in front of it, the
			// CRT resolves both against the same current directory
			if( !CoF_Lang_WAppend( out, size, &pos, cof_lang.wgame, wcslen( cof_lang.wgame )) ||
				!CoF_Lang_WAppendA( out, size, &pos, "/" ))
				return false;
		}

		if( !CoF_Lang_WAppendA( out, size, &pos, "languages/" ) ||
			!CoF_Lang_WAppend( out, size, &pos, cof_lang.wcode, wcslen( cof_lang.wcode )) ||
			!CoF_Lang_WAppendA( out, size, &pos, "/" ))
			return false;

		if( cof_lang_classes[cls].has_language_slot )
		{
			if( !CoF_Lang_WAppendA( out, size, &pos, "txt/" ) ||
				!CoF_Lang_WAppendA( out, size, &pos, cof_lang_classes[cls].folder ))
				return false;

			if( c == 0 )
			{
				if( !CoF_Lang_WAppendA( out, size, &pos, "languages/" ) ||
					!CoF_Lang_WAppend( out, size, &pos, cof_lang.wcode, wcslen( cof_lang.wcode )) ||
					!CoF_Lang_WAppendA( out, size, &pos, "/" ))
					return false;
			}
		}
		else
		{
			if( !CoF_Lang_WAppendA( out, size, &pos, cof_lang_classes[cls].folder ))
				return false;
		}

		if( !CoF_Lang_WAppend( out, size, &pos, rest, wcslen( rest )))
			return false;

		if( CoF_Lang_WIsFile( out ))
			return true;
	}

	return false;
}

static void CoF_Lang_WideToUtf8( char *out, size_t size, const wchar_t *in )
{
	if( WideCharToMultiByte( CP_UTF8, 0, in, -1, out, (int)size, NULL, NULL ) <= 0 )
		Q_strncpy( out, "<unprintable>", size );
}

/*
====================
CoF_Lang_CreateFileW

What the game DLLs' CreateFileW import points at while hooked.

Re-entrant and thread-safe: all state it needs is read under the shared
side of cof_lang.lock into locals, the rewritten path lives on this call's
stack, and the real open goes through the ENGINE's own CreateFileW import,
which is never patched, so the shim can never call itself. Logging happens
only on the main thread; other threads just count.
====================
*/
static HANDLE WINAPI CoF_Lang_CreateFileW( LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
	DWORD disposition, DWORD flags, HANDLE template_file )
{
	wchar_t redirected[1024];
	qboolean redirect, class_hit = false;
	DWORD err = GetLastError();

	AcquireSRWLockShared( &cof_lang.lock );
	redirect = CoF_Lang_RedirectPathW( name, access, disposition, redirected, ARRAYSIZE( redirected ), &class_hit );
	ReleaseSRWLockShared( &cof_lang.lock );

	if( redirect )
	{
		InterlockedIncrement( &cof_lang.redirects );

		if( GetCurrentThreadId( ) == cof_lang.main_thread )
		{
			if( host_developer.value )
			{
				char a[MAX_SYSPATH], b[MAX_SYSPATH];

				CoF_Lang_WideToUtf8( a, sizeof( a ), name );
				CoF_Lang_WideToUtf8( b, sizeof( b ), redirected );
				Con_DPrintf( "[cof-lang] open %s -> %s\n", a, b );
			}
		}
		else InterlockedIncrement( &cof_lang.offthread );

		name = redirected;
	}
	else if( class_hit )
	{
		InterlockedIncrement( &cof_lang.passthrough );

		if( host_developer.value >= DEV_EXTENDED && GetCurrentThreadId( ) == cof_lang.main_thread )
		{
			char a[MAX_SYSPATH];

			CoF_Lang_WideToUtf8( a, sizeof( a ), name );
			Con_Reportf( "[cof-lang] open %s (not in the pack, English)\n", a );
		}
	}

	if( cof_language_trace.value && GetCurrentThreadId( ) == cof_lang.main_thread )
	{
		char a[MAX_SYSPATH];

		CoF_Lang_WideToUtf8( a, sizeof( a ), name );
		CoF_Lang_Note( "[cof-lang] trace: CreateFileW %s (access 0x%lx, disposition %lu)%s\n", a, access, disposition, redirect ? " [pack]" : "" );
	}

	// logging above may have touched the last-error value the CRT reads back
	SetLastError( err );

	return CreateFileW( name, access, share, sa, disposition, flags, template_file );
}

/*
====================
CoF_Lang_FindImportSlot

The module's KERNEL32!CreateFileW import address table entry. Matched by
name through the import name table; a module bound without one is matched
by the resolved address instead.
====================
*/
static ULONG_PTR *CoF_Lang_FindImportSlot( HMODULE module )
{
	byte *base = (byte *)module;
	PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
	PIMAGE_NT_HEADERS nt;
	PIMAGE_IMPORT_DESCRIPTOR desc;
	IMAGE_DATA_DIRECTORY dir;
	FARPROC real = GetProcAddress( GetModuleHandleA( "kernel32.dll" ), "CreateFileW" );

	if( !base || dos->e_magic != IMAGE_DOS_SIGNATURE )
		return NULL;

	nt = (PIMAGE_NT_HEADERS)( base + dos->e_lfanew );
	if( nt->Signature != IMAGE_NT_SIGNATURE )
		return NULL;

	dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if( !dir.VirtualAddress || !dir.Size )
		return NULL;

	for( desc = (PIMAGE_IMPORT_DESCRIPTOR)( base + dir.VirtualAddress ); desc->Name; desc++ )
	{
		const char *dllname = (const char *)( base + desc->Name );
		PIMAGE_THUNK_DATA names, addrs;
		int i;

		if( Q_stricmp( dllname, "kernel32.dll" ))
			continue;

		addrs = (PIMAGE_THUNK_DATA)( base + desc->FirstThunk );
		names = desc->OriginalFirstThunk ? (PIMAGE_THUNK_DATA)( base + desc->OriginalFirstThunk ) : NULL;

		for( i = 0; addrs[i].u1.Function; i++ )
		{
			if( names )
			{
				PIMAGE_IMPORT_BY_NAME by_name;

				if( IMAGE_SNAP_BY_ORDINAL( names[i].u1.Ordinal ))
					continue;

				by_name = (PIMAGE_IMPORT_BY_NAME)( base + names[i].u1.AddressOfData );
				if( !Q_strcmp( (const char *)by_name->Name, "CreateFileW" ))
					return (ULONG_PTR *)&addrs[i].u1.Function;
			}
			else if( real && addrs[i].u1.Function == (ULONG_PTR)real )
			{
				return (ULONG_PTR *)&addrs[i].u1.Function;
			}
		}
	}

	return NULL;
}

static qboolean CoF_Lang_WriteSlot( ULONG_PTR *slot, ULONG_PTR value )
{
	DWORD old_protect;

	if( !VirtualProtect( slot, sizeof( *slot ), PAGE_READWRITE, &old_protect ))
		return false;

	InterlockedExchangePointer( (PVOID volatile *)slot, (PVOID)value );
	VirtualProtect( slot, sizeof( *slot ), old_protect, &old_protect );
	FlushInstructionCache( GetCurrentProcess( ), slot, sizeof( *slot ));
	return true;
}
#endif // XASH_WIN32

/*
====================
CoF_Lang_HookModule

Called right after LoadLibrary of client.dll (CL_LoadProgs) and of the
server library (SV_LoadProgs), before any of their exports is called.
====================
*/
void CoF_Lang_HookModule( void *hInstance, const char *name )
{
#if XASH_WIN32
	dll_user_t *hInst = (dll_user_t *)hInstance;
	const char *tag = COM_FileWithoutPath( name ? name : "?" );
	ULONG_PTR *slot;
	int i, free_slot = -1;

	if( !hInst || !hInst->hInstance )
		return;

	if( hInst->custom_loader )
	{
		CoF_Lang_Note( S_WARN "[cof-lang] %s was loaded from an archive by the custom loader; its file reads cannot be redirected\n", tag );
		return;
	}

	slot = CoF_Lang_FindImportSlot( (HMODULE)hInst->hInstance );
	if( !slot )
	{
		CoF_Lang_Note( S_WARN "[cof-lang] %s: no KERNEL32!CreateFileW import, its text files cannot be redirected\n", tag );
		return;
	}

	if( *slot == (ULONG_PTR)CoF_Lang_CreateFileW )
		return; // already ours

	for( i = 0; i < COF_LANG_MAX_HOOKS; i++ )
	{
		if( !cof_lang.hooks[i].module && free_slot < 0 )
			free_slot = i;
	}

	if( free_slot < 0 )
	{
		CoF_Lang_Note( S_ERROR "[cof-lang] %s: hook table full\n", tag );
		return;
	}

	cof_lang.hooks[free_slot].module = (HMODULE)hInst->hInstance;
	cof_lang.hooks[free_slot].slot = slot;
	cof_lang.hooks[free_slot].original = *slot;
	Q_strncpy( cof_lang.hooks[free_slot].tag, tag, sizeof( cof_lang.hooks[free_slot].tag ));

	if( !CoF_Lang_WriteSlot( slot, (ULONG_PTR)CoF_Lang_CreateFileW ))
	{
		CoF_Lang_Note( S_ERROR "[cof-lang] %s: cannot unprotect the import table (error %lu)\n", tag, GetLastError( ));
		memset( &cof_lang.hooks[free_slot], 0, sizeof( cof_lang.hooks[free_slot] ));
		return;
	}

	CoF_Lang_Note( "[cof-lang] %s: CreateFileW import hooked (IAT %p, was %p)\n", tag, (void *)slot, (void *)cof_lang.hooks[free_slot].original );
#endif
}

/*
====================
CoF_Lang_UnhookModule

Called from COM_FreeLibrary for every module, just before FreeLibrary.
Puts the original import back if this module is one we hooked.
====================
*/
void CoF_Lang_UnhookModule( void *module )
{
#if XASH_WIN32
	int i;

	if( !module )
		return;

	for( i = 0; i < COF_LANG_MAX_HOOKS; i++ )
	{
		cof_lang_hook_t *h = &cof_lang.hooks[i];

		if( h->module != (HMODULE)module )
			continue;

		if( *h->slot == (ULONG_PTR)CoF_Lang_CreateFileW )
		{
			if( CoF_Lang_WriteSlot( h->slot, h->original ))
				CoF_Lang_Note( "[cof-lang] %s: CreateFileW import restored\n", h->tag );
			else
				CoF_Lang_Note( S_WARN "[cof-lang] %s: could not restore the import (error %lu)\n", h->tag, GetLastError( ));
		}

		memset( h, 0, sizeof( *h ));
	}
#endif
}

/*
====================
CoF_Lang_ParseManifest

<gamedir>/languages/<code>/manifest.txt, key=value lines. The engine reads
display_name, codepage and authors; the rest is for people and tools.
====================
*/
static qboolean CoF_Lang_ParseManifest( const char *code, char *display, size_t display_size, char *authors, size_t authors_size, int *codepage )
{
	char path[MAX_QPATH];
	fs_offset_t len = 0;
	byte *raw;
	char *p, *end;

	Q_snprintf( path, sizeof( path ), "languages/%s/manifest.txt", code );
	raw = FS_LoadFile( path, &len, true );

	if( !raw )
		return false;

	Q_strncpy( display, code, display_size );
	authors[0] = '\0';
	*codepage = 1252;

	p = (char *)raw;
	end = p + len;

	while( p < end )
	{
		char *line = p;
		char *eq;
		size_t l;

		while( p < end && *p != '\n' )
			p++;

		*p = '\0';
		if( p < end )
			p++;

		l = Q_strlen( line );
		if( l && line[l - 1] == '\r' )
			line[l - 1] = '\0';

		if( !line[0] || line[0] == '#' )
			continue;

		eq = Q_strchr( line, '=' );
		if( !eq )
			continue;

		*eq++ = '\0';

		if( !Q_stricmp( line, "display_name" ))
			Q_strncpy( display, eq, display_size );
		else if( !Q_stricmp( line, "codepage" ))
			*codepage = Q_atoi( eq );
		else if( !Q_stricmp( line, "authors" ))
			Q_strncpy( authors, eq, authors_size );
	}

	Mem_Free( raw );

	if( *codepage != 1250 && *codepage != 1251 && *codepage != 1252 )
	{
		CoF_Lang_Note( S_WARN "cof_language: pack %s declares code page %i, which the font layer has no table for; using 1252\n", code, *codepage );
		*codepage = 1252;
	}

	return true;
}

/*
====================
CoF_Lang_Remount

Add the pack's overlay folder to the search path at the highest priority.
Called at the end of FS_Rescan_f, because a rescan rebuilds the search path
from gameinfo and drops anything added before it.

FWGS's own localisation mount (<gamedir>_<fs_language>, ui_language +
fs_mount_l10n + fs_rescan) cannot be used: it only ever mounts a SIBLING of
the game directory named after the language, while a pack lives inside the
game directory under languages/<code>/overlay/<gamedir>/ - pointing it
there would need a junction or a copy written into the install. Adding the
directory ourselves writes nothing to disk and goes away with the next
rescan.

The pack's overlay/ is rooted at the game ROOT (it holds <gamedir>/... and
platform/...), so the directory that lands on the search path is
overlay/<gamedir>/. platform/ is deliberately not mounted: the only files in
it are the Steam icons, and the platform directory is not this game's.
====================
*/
void CoF_Lang_Remount( void )
{
	char buf[MAX_QPATH * 2];
	search_t *t;

	if( !cof_lang.active )
		return;

	Q_snprintf( buf, sizeof( buf ), "%s/overlay/%s/*", cof_lang.dir, FS_Gamedir());
	t = FS_Search( buf, true, true );

	if( !t )
	{
		Con_DPrintf( "[cof-lang] %s has no overlay/%s folder\n", cof_lang.code, FS_Gamedir());
		return;
	}

	Mem_Free( t );

	Q_snprintf( buf, sizeof( buf ), "%s/%s/overlay/%s/", FS_Gamedir(), cof_lang.dir, FS_Gamedir());

	g_fsapi.AddGameDirectory( buf, FS_GAMEDIR_PATH | FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	CoF_Lang_Note( "[cof-lang] mounted %s at the top of the search path\n", buf );
}

/*
====================
CoF_Lang_Apply
====================
*/
static void CoF_Lang_Apply( void )
{
	const char *code = cof_language.string;
	char display[64], authors[256];
	int codepage = 1252;
	qboolean ok = false;

	if( !cof_lang.initialized )
		return;

	if( !COM_StringEmpty( code ))
	{
		if( Q_strchr( code, '/' ) || Q_strchr( code, '\\' ) || Q_strchr( code, '.' ) || Q_strchr( code, ':' ) || Q_strlen( code ) >= 60 )
			CoF_Lang_Note( S_ERROR "cof_language: \"%s\" is not a plain folder name\n", code );
		else if( !CoF_Lang_ParseManifest( code, display, sizeof( display ), authors, sizeof( authors ), &codepage ))
			CoF_Lang_Note( S_ERROR "cof_language: no pack at %s/languages/%s/manifest.txt, staying in English\n", FS_Gamedir(), code );
		else ok = true;

		if( !ok )
		{
			// clear the cvar so the frame check does not retry every frame
			Cvar_DirectSet( &cof_language, "" );
			code = cof_language.string;
		}
	}

#if XASH_WIN32
	AcquireSRWLockExclusive( &cof_lang.lock );
#endif

	cof_lang.active = ok;
	Q_strncpy( cof_lang.code, ok ? code : "", sizeof( cof_lang.code ));

	if( ok )
	{
		Q_snprintf( cof_lang.dir, sizeof( cof_lang.dir ), "languages/%s", code );
		Q_strncpy( cof_lang.display, display, sizeof( cof_lang.display ));
		Q_strncpy( cof_lang.authors, authors, sizeof( cof_lang.authors ));
		cof_lang.codepage = codepage;
	}
	else
	{
		cof_lang.dir[0] = cof_lang.display[0] = cof_lang.authors[0] = '\0';
		cof_lang.codepage = 1252;
	}

#if XASH_WIN32
	{
		const char *g = FS_Gamedir();
		int i;

		for( i = 0; cof_lang.code[i] && i < ARRAYSIZE( cof_lang.wcode ) - 1; i++ )
			cof_lang.wcode[i] = (wchar_t)(byte)cof_lang.code[i];
		cof_lang.wcode[i] = L'\0';

		for( i = 0; g[i] && i < ARRAYSIZE( cof_lang.wgame ) - 1; i++ )
			cof_lang.wgame[i] = (wchar_t)(byte)g[i];
		cof_lang.wgame[i] = L'\0';
	}

	ReleaseSRWLockExclusive( &cof_lang.lock );
#endif

	// the font layer decodes the pack's bytes with this; English is 1252
	if( Cvar_FindVar( "cof_text_codepage" ))
	{
		Cvar_Set( "cof_text_codepage", va( "%i", cof_lang.codepage ));
		cof_lang.codepage_pending = false;
	}
	else cof_lang.codepage_pending = true; // set from the first frame

	if( cof_lang.active )
	{
		CoF_Lang_Note( "cof_language: %s (%s, code page %i)%s%s\n", cof_lang.code, cof_lang.display, cof_lang.codepage,
			COM_StringEmpty( cof_lang.authors ) ? "" : ", by ", cof_lang.authors );
	}
	else
	{
		CoF_Lang_Note( "cof_language: off, English\n" );
	}

	CoF_Lang_LoadStrings();

	// rebuild the search path: this both drops the previous pack's overlay
	// and puts the new one on, and it is exactly what the stock fs_rescan
	// command does. CoF_Lang_Remount runs at the end of it.
	FS_Rescan_f();
}

static void CoF_Lang_Apply_f( void )
{
	// force a full re-read even when the code did not change, so a pack
	// edited on disk can be picked up without a restart
	CoF_Lang_Apply();
}

static void CoF_Lang_List_f( void )
{
	search_t *t = FS_Search( "languages/*", true, true );
	int i, found = 0;

	Con_Printf( "language packs under %s/languages:\n", FS_Gamedir());

	for( i = 0; t && i < t->numfilenames; i++ )
	{
		const char *code = COM_FileWithoutPath( t->filenames[i] );
		char display[64], authors[256];
		int codepage;

		if( !CoF_Lang_ParseManifest( code, display, sizeof( display ), authors, sizeof( authors ), &codepage ))
			continue;

		Con_Printf( "  %-12s %s, code page %i%s%s%s\n", code, display, codepage,
			COM_StringEmpty( authors ) ? "" : ", by ", authors,
			!Q_stricmp( code, cof_lang.code ) ? "  (active)" : "" );
		found++;
	}

	if( !found )
		Con_Printf( "  (none)\n" );

	if( t )
		Mem_Free( t );
}

static void CoF_Lang_Status_f( void )
{
	if( !cof_lang.active )
		Con_Printf( "cof_language: off, English\n" );
	else
		Con_Printf( "cof_language: %s (%s), code page %i, %i strings (%i formats), %i substitutions so far\n",
			cof_lang.code, cof_lang.display, cof_lang.codepage, cof_lang.num_entries, cof_lang.num_fmt, cof_lang.num_hits );

#if XASH_WIN32
	{
		int i;

		for( i = 0; i < COF_LANG_MAX_HOOKS; i++ )
		{
			if( cof_lang.hooks[i].module )
				Con_Printf( "  hook: %s CreateFileW at %p\n", cof_lang.hooks[i].tag, (void *)cof_lang.hooks[i].slot );
		}

		Con_Printf( "  redirected opens: %li (off the main thread: %li), pack-class opens left in English: %li\n",
			cof_lang.redirects, cof_lang.offthread, cof_lang.passthrough );
	}
#endif
}

void CoF_Lang_Frame( void )
{
	if( !cof_lang.initialized )
		return;

	if( Q_strcmp( cof_lang.code, cof_language.string ))
		CoF_Lang_Apply();

	// the pack was applied before the client registered cof_text_codepage
	// (CoF_Lang_EarlyApply), or config.cfg has since put an old value back
	if( cof_lang.codepage_pending && Cvar_FindVar( "cof_text_codepage" ))
	{
		Cvar_Set( "cof_text_codepage", va( "%i", cof_lang.codepage ));
		cof_lang.codepage_pending = false;
	}
}

/*
====================
CoF_Lang_EarlyValue

cof_language as the command line or config.cfg will set it, read before
either has been executed: "+cof_language <code>" or "+set cof_language
<code>" on the command line win over a cof_language line in config.cfg.
====================
*/
static qboolean CoF_Lang_EarlyValue( char *out, size_t size )
{
	byte *raw;
	char *p;
	qboolean found = false;
	int i;

	for( i = 1; i < host.argc; i++ )
	{
		if( !Q_stricmp( host.argv[i], "+cof_language" ) && i + 1 < host.argc )
		{
			Q_strncpy( out, host.argv[i + 1], size );
			found = true;
		}
		else if( !Q_stricmp( host.argv[i], "+set" ) && i + 2 < host.argc && !Q_stricmp( host.argv[i + 1], "cof_language" ))
		{
			Q_strncpy( out, host.argv[i + 2], size );
			found = true;
		}
	}

	if( found )
	{
		// "+set cof_language """ arrives as a literal pair of quotes
		if( !Q_strcmp( out, "\"\"" ))
			out[0] = '\0';
		return true;
	}

	raw = FS_LoadFile( "config.cfg", NULL, true );
	if( !raw )
		return false;

	for( p = (char *)raw; p && *p; )
	{
		char line[256], token[MAX_TOKEN];
		char *end = Q_strchr( p, '\n' );
		size_t len = end ? (size_t)( end - p ) : Q_strlen( p );
		char *lp;

		Q_strncpy( line, p, Q_min( len + 1, sizeof( line )));
		p = end ? end + 1 : NULL;

		lp = COM_ParseFile( line, token, sizeof( token ));
		if( !lp || Q_stricmp( token, "cof_language" ))
			continue;

		token[0] = '\0';
		COM_ParseFile( lp, token, sizeof( token ));
		Q_strncpy( out, token, size );
		found = true; // the last one wins, as it would when exec'd
	}

	Mem_Free( raw );
	return found;
}

/*
====================
CoF_Lang_EarlyApply

Called from Host_Main after the game directory is mounted and BEFORE the
server and client libraries are loaded. The client builds its fonts, VGUI
panels and art from HUD_Init / the first VidInit, before config.cfg is exec'd
or the first frame runs; a pack applied only at the first frame would be too
late for all of that and for anything else the game caches at init.
====================
*/
void CoF_Lang_EarlyApply( void )
{
	char value[64];

	if( !cof_lang.initialized || !CoF_Lang_EarlyValue( value, sizeof( value )))
		return;

	if( COM_StringEmpty( value ))
		return; // English: nothing to do, and nothing to rescan

	Cvar_DirectSet( &cof_language, value );
	CoF_Lang_Apply();
}

void CoF_Lang_Init( void )
{
	if( cof_lang.initialized )
		return;

#if XASH_WIN32
	InitializeSRWLock( &cof_lang.lock );
	cof_lang.main_thread = GetCurrentThreadId( );
#endif

	Cvar_RegisterVariable( &cof_language );
	Cvar_RegisterVariable( &cof_language_strings );
	Cvar_RegisterVariable( &cof_language_files );
	Cvar_RegisterVariable( &cof_language_trace );

	Cmd_AddRestrictedCommand( "cof_language_apply", CoF_Lang_Apply_f, "re-read and re-mount the active Cry of Fear language pack" );
	Cmd_AddCommand( "cof_language_list", CoF_Lang_List_f, "list the language packs found under <gamedir>/languages" );
	Cmd_AddCommand( "cof_language_status", CoF_Lang_Status_f, "show the active language pack, its hooks and its counters" );

	cof_lang.initialized = true;
}
