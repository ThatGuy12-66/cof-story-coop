/*
MenuStrings.cpp - custom menu strings
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <ctype.h>
#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "MenuStrings.h"
#include "utlhashmap.h"
#include "generichash.h"
#include "utflib.h"

#define EMPTY_STRINGS_1 ""
#define EMPTY_STRINGS_2 EMPTY_STRINGS_1, EMPTY_STRINGS_1
#define EMPTY_STRINGS_5 EMPTY_STRINGS_2, EMPTY_STRINGS_2, EMPTY_STRINGS_1
#define EMPTY_STRINGS_10 EMPTY_STRINGS_5, EMPTY_STRINGS_5
#define EMPTY_STRINGS_20 EMPTY_STRINGS_10, EMPTY_STRINGS_10
#define EMPTY_STRINGS_50 EMPTY_STRINGS_20, EMPTY_STRINGS_20, EMPTY_STRINGS_10
#define EMPTY_STRINGS_100 EMPTY_STRINGS_50, EMPTY_STRINGS_50

static CUtlHashMap<const char *, const char *> hashed_cmds;

const char *MenuStrings[IDS_LAST] =
{
EMPTY_STRINGS_100, // 0..99
EMPTY_STRINGS_50, // 100..149
EMPTY_STRINGS_20, // 150..169
EMPTY_STRINGS_10, // 170..179
EMPTY_STRINGS_5, // 180..184
EMPTY_STRINGS_2, // 185..186
EMPTY_STRINGS_1, // 187
"Return to game.", // 188
"Start a new game.", // 189
EMPTY_STRINGS_1,	// 190
"Load a previously saved game.", // 191
"Load a saved game, save the current game.", // 192
"Change game settings, configure controls", // 193
EMPTY_STRINGS_20, // 194..213
EMPTY_STRINGS_20, // 214..233
"Starting a Hazard Course will exit\nany current game, OK to exit?", // 234
EMPTY_STRINGS_5, // 235..239
"Starting a new game will exit\nany current game, OK to exit?",	// 240
EMPTY_STRINGS_5, // 241..245
EMPTY_STRINGS_2, // 246..247
EMPTY_STRINGS_2, // 248..249
EMPTY_STRINGS_100, // 250..349
EMPTY_STRINGS_50, // 350..399
"Find more about Valve's product lineup",	// 400
EMPTY_STRINGS_1, // 401
"http://store.steampowered.com/app/70/", // 402
EMPTY_STRINGS_5, // 403..407
EMPTY_STRINGS_2, // 408..409
EMPTY_STRINGS_100, // 410..509
EMPTY_STRINGS_20, // 510..529
"Select a custom game",	// 530
EMPTY_STRINGS_5, // 531..535
EMPTY_STRINGS_2, // 536..537
EMPTY_STRINGS_2, // 538..539
EMPTY_STRINGS_50, // 540..589
EMPTY_STRINGS_10, // 590..599
};

const char *L( const char *szStr ) // L means Localize!
{
	if( szStr )
	{
		if( *szStr == '#' )
			szStr++;

		int i = hashed_cmds.Find( szStr );

		if( i != hashed_cmds.InvalidIndex() )
			return hashed_cmds[i];
	}

	return szStr;
}

static void Dictionary_Insert( const char *key, const char *value )
{
	int i = hashed_cmds.Find( key );

	// don't allow dupes, delete older strings
	if( i != hashed_cmds.InvalidIndex() )
	{
		const char *old = hashed_cmds[i];

		hashed_cmds[i] = StringCopy( value );

		delete[] old;
	}
	else
	{
		const char *first = StringCopy( key );
		const char *second = StringCopy( value );

		hashed_cmds.Insert( first, second );
	}
}

static void UI_InitAliasStrings( void )
{
	const struct
	{
		const char *defAliasString;
		int idx;
	} aliasStrings[] =
	{
	{ "Quit %s without\nsaving current game?", IDS_MAIN_QUITPROMPTINGAME },
	{ "Learn how to play %s", IDS_MAIN_TRAININGHELP },
	{ "Play %s on the 'easy' skill setting", IDS_NEWGAME_EASYHELP },
	{ "Play %s on the 'medium' skill setting", IDS_NEWGAME_MEDIUMHELP },
	{ "Play %s on the 'difficult' skill setting", IDS_NEWGAME_DIFFICULTHELP },
	{ "Quit playing %s", IDS_MAIN_QUITHELP },
	{ "Search for %s servers, configure character", IDS_MAIN_MULTIPLAYERHELP },
	};

	for( int i = 0; i < V_ARRAYSIZE( aliasStrings ); i++ )
	{
		if( MenuStrings[aliasStrings[i].idx][0]) // check if not initialized by strings.lst
			continue;

		char token[64];
		snprintf( token, sizeof( token ), "StringsList_%d", aliasStrings[i].idx );

		const char *fmt_str = L( token );
		if( fmt_str == token ) // not found
			fmt_str = aliasStrings[i].defAliasString;

		CUtlString fmt( fmt_str );
		fmt.Replace( "%s", gMenu.m_gameinfo.title );
		MenuStrings[aliasStrings[i].idx] = fmt.DetachRawPtr();

		Dictionary_Insert( token, MenuStrings[aliasStrings[i].idx] );
	}
}

int UTFToCP1251( char *out, const char *instr, int len, int maxoutlen )
{
	int m = -1, k = 0, uc = 0;
	const char *inbegin = instr;
	char *outbegin = out;

	while( *instr && (out - outbegin) < maxoutlen )
	{
		int in = *instr & 255;
		if( instr - inbegin >= len )
		{
			*out = 0;
			return out - outbegin;
		}

		// Get character length
		if(m == -1)
		{
			uc = 0;
			if( in >= 0xF8 )
			{
				instr++;
				continue;
			}
			else if( in >= 0xF0 )
			{
				uc = in & 0x07;
				m = 3;
			}
			else if( in >= 0xE0 )
			{
				uc = in & 0x0F;
				m = 2;
			}
			else if( in >= 0xC0 )
			{
				uc = in & 0x1F;
				m = 1;
			}
			else if( in <= 0x7F)
			{
				if( in == '\\' && *(instr + 1) != '\\'  )
				{
					instr++;
					continue;
				}
				*out++ = *instr++;
				continue;
			}
			// we need more chars to decode one
			k=0;
			instr++;
			continue;
		}
		// get more chars
		else if( k <= m )
		{
			uc <<= 6;
			uc += in & 0x3F;
			k++;
		}
		if( in > 0xBF || m < 0 )
		{
			m = -1;
			instr++;
			continue;
		}


		if( k == m )
		{
			k = m = -1;
			// cp1252 hackish translate
			if( uc > 0x7F && uc <= 0xFF )
			{
				*out++ = uc;
				instr++;
				continue;
			}

			// cp1251
			if( uc >= 0x0410 && uc <= 0x042F ) // Cyrillic Capital Letters
			{
				*out++ = uc - 0x410 + 0xC0;
				instr++;
				continue;
			}
			else if( uc >= 0x0430 && uc <= 0x044F ) // Cyrillic Small Letters
			{
				*out++ = uc - 0x430 + 0xE0;
				instr++;
				continue;
			}
			else
			{
				int i;
				for( i = 0; i < 64; i++ )
				{
					if( table_cp1251[i] == uc )
					{
						*out++ = i + 0x80;
						instr++;
						break;
					}
				}
				continue;
			}

			*out++ = *instr++;
			continue;
		}
		instr++;
	}
	*out = 0;
	return out - outbegin;
}

static uint Localize_ProcessString( char *dst, const char *src )
{
	const char *p;
	uint i = 0;

	p = src;

	while( *p )
	{
		if( *p == '\\' )
		{
			char replace = 0;

			switch( p[1] )
			{
			case '\\': replace = '\\'; break;
			case 'n': replace = '\n'; break;
			}

			if( replace )
			{
				if( dst )
					dst[i] = replace;
				i++;
				p += 2;
				continue;
			}
		}

		if( dst )
			dst[i] = *p;
		i++;
		p++;
	}

	// null terminator
	if( dst )
		dst[i] = '\0';
	i++;

	return i;
}

static void ByteSwapUTF16File( uint16_t *head, size_t len )
{
	for( size_t i = 0; i < len; i++ )
		head[i] = Swap16( head[i] );
}

static void Localize_AddToDictionary( const char *name, const char *lang )
{
	char filename[64], token[4096];
	char *pfile, *afile = nullptr, *pFileBuf;
	int i = 0, buflen, charlen;
	bool isUtf16 = false;

	snprintf( filename, sizeof( filename ), "resource/%s_%s.txt", name, lang );

	pFileBuf = reinterpret_cast<char*>( EngFuncs::COM_LoadFile( filename, &buflen ));

	if( !pFileBuf )
	{
		Con_Printf( "Localize_AddToDict( %s ): couldn't open file. Some strings will not be localized!.\n", filename );
		return;
	}

	if( pFileBuf[0] == '\xFF' && pFileBuf[1] == '\xFE' )
	{
		if( buflen > 3 && !pFileBuf[2] && !pFileBuf[3] )
		{
			Con_Printf( "Localize_AddToDict( %s ): couldn't parse file. UTF-32 little endian isn't supported\n", filename );
			goto error;
		}

		charlen = buflen / 2 - 1;
		isUtf16 = true;

#if XASH_BIG_ENDIAN
		ByteSwapUTF16File( (uint16_t *)pFileBuf, charlen );
#endif
	}
	else if( pFileBuf[0] == '\xFE' && pFileBuf[1] == '\xFF' )
	{
		if( buflen > 3 && !pFileBuf[2] && !pFileBuf[3] )
		{
			Con_Printf( "Localize_AddToDict( %s ): couldn't parse file. UTF-32 big endian isn't supported\n", filename );
			goto error;
		}

		charlen = buflen / 2 - 1;
		isUtf16 = true;

#if XASH_LITTLE_ENDIAN
		ByteSwapUTF16File( (uint16_t *)pFileBuf, charlen );
#endif
	}

	if( isUtf16 )
	{
		size_t utf8len = Q_UTF16ToUTF8( NULL, 0, (const uint16_t *)&pFileBuf[2], charlen );

		// Con_Printf( "size in utf16: %zu (%zu), size in utf8: %zu\n", buflen, charlen, utf8len );

		afile = new char[utf8len + 1]; // save original pointer, so we can free it later

		Q_UTF16ToUTF8( afile, utf8len + 1, (const uint16_t *)&pFileBuf[2], charlen );
	}
	else
	{
		afile = pFileBuf;

		// strip UTF-8 BOM
		if( afile[0] == '\xEF' && afile[1] == '\xBB' && afile[2] == '\xBF')
			afile += 3;
	}

	pfile = afile;

	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));

	if( stricmp( token, "lang" ))
	{
		Con_Printf( "Localize_AddToDict( %s ): invalid header, got %s", filename, token );
		goto error;
	}

	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ) );

	if( strcmp( token, "{" ))
	{
		Con_Printf( "Localize_AddToDict( %s ): want {, got %s", filename, token );
		goto error;
	}

	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ) );

	if( stricmp( token, "Language" ))
	{
		Con_Printf( "Localize_AddToDict( %s ): want Language, got %s", filename, token );
		goto error;
	}

	// skip language actual name
	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ) );

	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ) );

	if( stricmp( token, "Tokens" ))
	{
		Con_Printf( "Localize_AddToDict( %s ): want Tokens, got %s", filename, token );
		goto error;
	}

	pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ) );

	if( strcmp( token, "{" ))
	{
		Con_Printf( "Localize_AddToDict( %s ): want { after Tokens, got %s", filename, token );
		goto error;
	}

	while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))))
	{
		if( !strcmp( token, "}" ))
			break;

		char szLocString[4096];
		pfile = EngFuncs::COM_ParseFile( pfile, szLocString, sizeof( szLocString ));

		if( !strcmp( szLocString, "}" ))
			break;

		if( pfile )
		{
			// Con_DPrintf("New token: %s %s\n", token, szLocString );
			Localize_ProcessString( token, token );
			Localize_ProcessString( szLocString, szLocString );
			Dictionary_Insert( token, szLocString );
			i++;
		}
	}

error:
	if( isUtf16 && afile )
		delete[] afile;

	EngFuncs::COM_FreeFile( pFileBuf );
}

static void Localize_InitLanguage( const char *language )
{
	const char *gamedir = gMenu.m_gameinfo.gamefolder;

	// if gamedir isn't gameui, then load standard gameui strings
	if( strcmp( gamedir, "gameui" ))
		Localize_AddToDictionary( "gameui", language );

	// if gamedir isn't valve, then load standard HL1 strings
	if( strcmp( gamedir, "valve" ))
		Localize_AddToDictionary( "valve",  language );

	// if gamedir isn't mainui, then load standard mainui strings
	if( strcmp( gamedir, "mainui" ))
		Localize_AddToDictionary( "mainui", language );

	// mod strings override default ones
	Localize_AddToDictionary( gamedir,  language );
}

static void Localize_Init( void )
{
	EngFuncs::ClientCmd( true, "exec mainui.cfg\n" );

	hashed_cmds.Purge();

	// strings.lst first
	for( int i = 0; i < IDS_LAST; i++ )
	{
		if( !MenuStrings[i][0] )
			continue;

		char buf[256];

		snprintf( buf, sizeof( buf ), "StringsList_%i", i );

		Dictionary_Insert( buf, MenuStrings[i] );
	}

	// always load default language translation
	Localize_InitLanguage( "english" );

	const char *language = EngFuncs::GetCvarString( "ui_language" );

	if( language[0] && strcmp( language, "english" ))
		Localize_InitLanguage( language );

	// strings.lst compatible aliasstrings then
	UI_InitAliasStrings ();
}

static void Localize_Free( void )
{
	FOR_EACH_HASHMAP( hashed_cmds, i )
	{
		const char *first = hashed_cmds.Key( i );
		const char *second = hashed_cmds.Element( i );

		delete[] (char*)first;
		delete[] (char*)second;
	}

	hashed_cmds.Purge();
}

void UI_LoadCustomStrings( void )
{
	char *afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/strings.lst", NULL );
	char *pfile = afile;
	char token[1024];
	int string_num;

	if( !afile )
		goto localize_init;

	while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( isdigit( token[0] ))
		{
			string_num = atoi( token );

			// check for bad stiringnum
			if( string_num < 0 ) continue;
			if( string_num > ( IDS_LAST - 1 ))
				continue;
		}
		else continue; // invalid declaration ?

		// parse new string
		pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
		MenuStrings[string_num] = StringCopy( token ); // replace default string with custom
	}

	EngFuncs::COM_FreeFile( afile );

localize_init:
	Localize_Init();
}

void UI_FreeCustomStrings( void )
{
	Localize_Free();
}

/*
==============================================================================

Cry of Fear: the menu's own strings from the active language pack

The engine's archived cof_language names a pack under <gamedir>/languages/
(docs/cof-language-packs.md). The pack's strings/menu-strings.tsv translates
this menu library's own text:

	<English, exactly as the menu draws it><TAB><translation>[<TAB>notes...]

UTF-8 without or with a BOM; a line that is not valid UTF-8 is read in the
code page the pack's manifest.txt declares (1250, 1251 or 1252). Lines that
start with '#' and a first line whose first cell is "english" are skipped;
\n, \t and \\ are unescaped on both sides; an empty translation, or one equal
to the English, is ignored. A key with printf conversions (%s %d %i %u %f %c
%x) also matches formatted text: its literal pieces must appear in order,
the first at the start and the last at the end; the text in between goes into
the translation's own conversions left to right (surplus ones are dropped),
and a captured piece that is itself a key is translated too. Longer literal
keys are tried first. The same rules as the engine's DLL string table.

The key is the English as drawn - for a "#GameUI_..." token, the English value
the game's resource file gives it - so pack authors work from what they see;
scripts/polish/extract-menu-strings.ps1 lists every one of them.

The lookup is applied where text is drawn and measured (UI_DrawString,
CFontManager::GetTextWide and GetTextHeightExt), not inside L(): every page
resolves its L() strings once, the first time it opens, so a lookup there
could not follow a change of cof_language in a running game. At the draw
layer UI_LangFrame notices the change, reloads the table and BaseMenu.cpp
re-lays out the open pages, whose widths depend on the text. Editable text
(CMenuField) is held out with UI_LangHold.

==============================================================================
*/

#define LANG_MAX_CONV	8
#define LANG_OUT_SIZE	2048
#define LANG_RING	16

struct langPattern_t
{
	const char	*value;			// translation, UTF-8 (owned)
	char		*pieces;		// the key's literal pieces, back to back (owned)
	const char	*piece[LANG_MAX_CONV + 1];
	int		pieceLen[LANG_MAX_CONV + 1];
	int		numConv;
	int		literal;		// total literal bytes, longer keys first
};

static CUtlHashMap<const char *, const char *> s_langExact;
static langPattern_t *s_langPatterns;
static int s_langNumPatterns;
static char s_langCode[64];
static bool s_langLoaded;
static int s_langHold;
static char s_langRing[LANG_RING][LANG_OUT_SIZE];
static int s_langRingPos;

// 0x80..0xFF of the three code pages the engine's font layer knows
static const unsigned short s_langCP1250[128] =
{
	0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021, 0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

static const unsigned short s_langCP1251[128] =
{
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

static const unsigned short s_langCP1252[32] =
{
	0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
	0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

static bool Lang_IsUTF8( const char *s )
{
	const unsigned char *p = (const unsigned char *)s;

	while( *p )
	{
		int n;

		if( *p < 0x80 ) { p++; continue; }
		else if(( *p & 0xE0 ) == 0xC0 && *p >= 0xC2 ) n = 1;
		else if(( *p & 0xF0 ) == 0xE0 ) n = 2;
		else if(( *p & 0xF8 ) == 0xF0 && *p <= 0xF4 ) n = 3;
		else return false;

		for( p++; n > 0; n--, p++ )
		{
			if(( *p & 0xC0 ) != 0x80 )
				return false;
		}
	}

	return true;
}

// one byte string in the pack's code page to UTF-8
static void Lang_CodepageToUTF8( char *out, size_t size, const char *in, int codepage )
{
	size_t o = 0;

	for( const unsigned char *p = (const unsigned char *)in; *p && o + 4 < size; p++ )
	{
		unsigned int uc = *p;

		if( uc >= 0x80 )
		{
			if( codepage == 1250 )
				uc = s_langCP1250[uc - 0x80];
			else if( codepage == 1251 )
				uc = s_langCP1251[uc - 0x80];
			else if( uc < 0xA0 )
				uc = s_langCP1252[uc - 0x80];
		}

		if( uc < 0x80 )
			out[o++] = (char)uc;
		else if( uc < 0x800 )
		{
			out[o++] = (char)( 0xC0 | ( uc >> 6 ));
			out[o++] = (char)( 0x80 | ( uc & 0x3F ));
		}
		else
		{
			out[o++] = (char)( 0xE0 | ( uc >> 12 ));
			out[o++] = (char)( 0x80 | (( uc >> 6 ) & 0x3F ));
			out[o++] = (char)( 0x80 | ( uc & 0x3F ));
		}
	}

	out[o] = 0;
}

static void Lang_Unescape( char *s )
{
	char *d = s;

	for( ; *s; s++ )
	{
		if( s[0] == '\\' && ( s[1] == 'n' || s[1] == 't' || s[1] == 'r' || s[1] == '\\' ))
		{
			s++;
			*d++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'r' ? '\r' : '\\';
			continue;
		}
		*d++ = *s;
	}

	*d = 0;
}

// length of the printf conversion starting at s ('%'), or 0 if there is none
static int Lang_ConversionLength( const char *s )
{
	const char *p = s + 1;

	while( *p && strchr( "-+ #0", *p ))
		p++;
	while( *p >= '0' && *p <= '9' )
		p++;
	if( *p == '.' )
	{
		p++;
		while( *p >= '0' && *p <= '9' )
			p++;
	}
	while( *p == 'l' || *p == 'h' )
		p++;

	if( *p && strchr( "sdiufcxX", *p ))
		return (int)( p - s ) + 1;

	return 0;
}

static void Lang_Free( void )
{
	FOR_EACH_HASHMAP( s_langExact, i )
	{
		delete[] (char *)s_langExact.Key( i );
		delete[] (char *)s_langExact.Element( i );
	}
	s_langExact.Purge();

	for( int i = 0; i < s_langNumPatterns; i++ )
	{
		delete[] (char *)s_langPatterns[i].value;
		delete[] s_langPatterns[i].pieces;
	}
	delete[] s_langPatterns;
	s_langPatterns = NULL;
	s_langNumPatterns = 0;
}

// split a key into literal pieces around its conversions; false = exact only
static bool Lang_ParsePattern( langPattern_t *pat, const char *key )
{
	size_t len = strlen( key );
	char *buf = new char[len + LANG_MAX_CONV + 2];
	char *d = buf;
	int n = 0;

	memset( pat, 0, sizeof( *pat ));
	pat->pieces = buf;
	pat->piece[0] = d;

	for( const char *s = key; *s; )
	{
		if( s[0] == '%' && s[1] == '%' )
		{
			*d++ = '%';
			s += 2;
			continue;
		}

		int conv = s[0] == '%' ? Lang_ConversionLength( s ) : 0;

		if( !conv )
		{
			*d++ = *s++;
			continue;
		}

		if( n >= LANG_MAX_CONV )
			break;

		*d++ = 0;
		pat->pieceLen[n] = (int)strlen( pat->piece[n] );
		pat->literal += pat->pieceLen[n];
		n++;
		pat->piece[n] = d;
		s += conv;
	}

	*d = 0;
	pat->pieceLen[n] = (int)strlen( pat->piece[n] );
	pat->literal += pat->pieceLen[n];
	pat->numConv = n;

	// two adjacent conversions cannot be told apart, and a key of almost
	// nothing but conversions would match far too much
	bool ok = n > 0 && pat->literal >= 3;

	for( int i = 1; ok && i < n; i++ )
	{
		if( !pat->pieceLen[i] )
			ok = false;
	}

	if( !ok )
	{
		delete[] buf;
		pat->pieces = NULL;
	}

	return ok;
}

static int Lang_ReadCodepage( const char *code )
{
	char path[128];
	int length = 0, codepage = 1252;

	snprintf( path, sizeof( path ), "languages/%s/manifest.txt", code );
	char *raw = (char *)EngFuncs::COM_LoadFile( path, &length );

	if( !raw )
		return codepage;

	for( const char *p = raw, *end = raw + length; p < end; )
	{
		const char *line = p;

		while( p < end && *p != '\n' )
			p++;
		if( p < end )
			p++;

		if( end - line > 9 && !strnicmp( line, "codepage=", 9 ))
		{
			codepage = atoi( line + 9 );
			break;
		}
	}

	EngFuncs::COM_FreeFile( raw );
	return codepage;
}

static void Lang_Load( const char *code )
{
	char path[128];
	int length = 0, exact = 0, skipped = 0;
	int codepage = 0;

	Lang_Free();
	Q_strncpy( s_langCode, code, sizeof( s_langCode ));
	s_langLoaded = true;

	if( !code[0] )
		return;

	// a pack code is a plain folder name, the rule the engine applies
	if( strpbrk( code, ".:\\/" ))
	{
		Con_Printf( "menu strings: \"%s\" is not a pack folder name, the menu stays in English\n", code );
		return;
	}

	snprintf( path, sizeof( path ), "languages/%s/strings/menu-strings.tsv", code );
	char *raw = (char *)EngFuncs::COM_LoadFile( path, &length );

	if( !raw )
	{
		Con_Printf( "menu strings: no %s, the menu stays in English\n", path );
		return;
	}

	// a private, terminated copy the lines are cut in place from
	char *text = new char[length + 1];
	memcpy( text, raw, length );
	text[length] = 0;
	EngFuncs::COM_FreeFile( raw );

	char *p = text;
	if( (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF )
		p += 3;

	int maxPatterns = 0;
	for( char *s = p; *s; s++ )
	{
		if( *s == '\n' )
			maxPatterns++;
	}
	s_langPatterns = new langPattern_t[maxPatterns + 1];

	bool first = true;

	while( *p )
	{
		char *line = p;

		while( *p && *p != '\n' )
			p++;
		if( *p )
			*p++ = 0;

		size_t n = strlen( line );
		if( n && line[n - 1] == '\r' )
			line[n - 1] = 0;

		if( !line[0] || line[0] == '#' )
			continue;

		char *value = strchr( line, '\t' );
		if( !value )
		{
			skipped++;
			continue;
		}
		*value++ = 0;

		char *tail = strchr( value, '\t' );
		if( tail )
			*tail = 0;

		if( first )
		{
			first = false;
			if( !stricmp( line, "english" ))
				continue;
		}

		char key[LANG_OUT_SIZE], val[LANG_OUT_SIZE];

		if( Lang_IsUTF8( line ) && Lang_IsUTF8( value ))
		{
			Q_strncpy( key, line, sizeof( key ));
			Q_strncpy( val, value, sizeof( val ));
		}
		else
		{
			if( !codepage )
				codepage = Lang_ReadCodepage( code );
			Lang_CodepageToUTF8( key, sizeof( key ), line, codepage );
			Lang_CodepageToUTF8( val, sizeof( val ), value, codepage );
		}

		Lang_Unescape( key );
		Lang_Unescape( val );

		if( !key[0] || !val[0] || !strcmp( key, val ))
			continue;

		// the first of duplicate rows wins
		if( s_langExact.Find( key ) != s_langExact.InvalidIndex( ))
			continue;

		s_langExact.Insert( StringCopy( key ), StringCopy( val ));
		exact++;

		langPattern_t pat;
		if( strchr( key, '%' ) && Lang_ParsePattern( &pat, key ))
		{
			pat.value = StringCopy( val );
			s_langPatterns[s_langNumPatterns++] = pat;
		}
	}

	delete[] text;

	// longer literal text first, so "You got the %s" beats "You got %s"
	for( int i = 1; i < s_langNumPatterns; i++ )
	{
		langPattern_t t = s_langPatterns[i];
		int j = i - 1;

		while( j >= 0 && s_langPatterns[j].literal < t.literal )
		{
			s_langPatterns[j + 1] = s_langPatterns[j];
			j--;
		}
		s_langPatterns[j + 1] = t;
	}

	Con_Printf( "menu strings: %s: %d strings, %d of them with conversions%s\n",
		path, exact, s_langNumPatterns, skipped ? " (some lines had no tab and were skipped)" : "" );
}

static bool Lang_Match( const langPattern_t *pat, const char *text, int len, const char **cap, int *capLen )
{
	if( len < pat->literal )
		return false;

	if( strncmp( text, pat->piece[0], pat->pieceLen[0] ))
		return false;

	const char *pos = text + pat->pieceLen[0];
	const char *last = text + len - pat->pieceLen[pat->numConv];

	if( last < pos || strncmp( last, pat->piece[pat->numConv], pat->pieceLen[pat->numConv] ))
		return false;

	for( int i = 1; i < pat->numConv; i++ )
	{
		const char *found = NULL;

		// every capture holds at least one byte
		for( const char *s = pos + 1; s + pat->pieceLen[i] <= last; s++ )
		{
			if( !strncmp( s, pat->piece[i], pat->pieceLen[i] ))
			{
				found = s;
				break;
			}
		}

		if( !found )
			return false;

		cap[i - 1] = pos;
		capLen[i - 1] = (int)( found - pos );
		pos = found + pat->pieceLen[i];
	}

	if( last <= pos )
		return false;

	cap[pat->numConv - 1] = pos;
	capLen[pat->numConv - 1] = (int)( last - pos );
	return true;
}

static const char *Lang_Format( const langPattern_t *pat, const char **cap, const int *capLen )
{
	char *out = s_langRing[s_langRingPos];
	size_t o = 0;
	int k = 0;

	s_langRingPos = ( s_langRingPos + 1 ) % LANG_RING;

	for( const char *s = pat->value; *s && o + 1 < LANG_OUT_SIZE; )
	{
		if( s[0] == '%' && s[1] == '%' )
		{
			out[o++] = '%';
			s += 2;
			continue;
		}

		int conv = s[0] == '%' ? Lang_ConversionLength( s ) : 0;

		if( !conv )
		{
			out[o++] = *s++;
			continue;
		}

		s += conv;

		if( k >= pat->numConv )
			continue;	// surplus conversions are removed, never drawn

		// a captured piece that is itself a key is translated too
		char piece[LANG_OUT_SIZE];
		int n = Q_min( capLen[k], (int)sizeof( piece ) - 1 );

		memcpy( piece, cap[k], n );
		piece[n] = 0;
		k++;

		const char *put = piece;
		int i = s_langExact.Find( piece );
		if( i != s_langExact.InvalidIndex( ))
			put = s_langExact[i];

		for( ; *put && o + 1 < LANG_OUT_SIZE; put++ )
			out[o++] = *put;
	}

	out[o] = 0;
	return out;
}

/*
=================
UI_LangText

The active pack's translation of a string the menu is about to draw or
measure, or the string itself.
=================
*/
static const char *Lang_Lookup( const char *text )
{
	int i = s_langExact.Find( text );

	if( i != s_langExact.InvalidIndex( ))
		return s_langExact[i];

	if( !s_langNumPatterns )
		return NULL;

	int len = (int)strlen( text );

	if( len >= LANG_OUT_SIZE )
		return NULL;

	for( int j = 0; j < s_langNumPatterns; j++ )
	{
		const char *cap[LANG_MAX_CONV];
		int capLen[LANG_MAX_CONV];

		if( Lang_Match( &s_langPatterns[j], text, len, cap, capLen ))
			return Lang_Format( &s_langPatterns[j], cap, capLen );
	}

	return NULL;
}

/*
=================
Lang_LookupLines

A text the pages build from pieces (the co-op Continue page's detail: a
title line, "Saved by ...", a warning) has no row of its own: each of its
lines is looked up instead, exactly or by pattern. NULL when no line has a
translation.
=================
*/
static const char *Lang_LookupLines( const char *text )
{
	char line[LANG_OUT_SIZE];
	char joined[LANG_OUT_SIZE];
	size_t o = 0;
	bool changed = false;

	for( const char *s = text; ; )
	{
		const char *end = strchr( s, '\n' );
		size_t n = end ? (size_t)( end - s ) : strlen( s );

		if( n >= sizeof( line ))
			return NULL;

		memcpy( line, s, n );
		line[n] = 0;

		const char *put = n ? Lang_Lookup( line ) : NULL;

		if( put )
			changed = true;
		else
			put = line;

		for( ; *put && o + 1 < sizeof( joined ); put++ )
			joined[o++] = *put;

		if( !end )
			break;

		if( o + 1 < sizeof( joined ))
			joined[o++] = '\n';
		s = end + 1;
	}

	if( !changed )
		return NULL;

	joined[o] = 0;

	char *out = s_langRing[s_langRingPos];
	s_langRingPos = ( s_langRingPos + 1 ) % LANG_RING;
	Q_strncpy( out, joined, LANG_OUT_SIZE );
	return out;
}

const char *UI_LangText( const char *text )
{
	if( !text || !text[0] || s_langHold > 0 || !s_langExact.Count( ))
		return text;

	const char *out = Lang_Lookup( text );

	if( out )
		return out;

	if( strchr( text, '\n' ) && ( out = Lang_LookupLines( text )))
		return out;

	// text wrapped in ^N colour codes (the Controls list builds its rows as
	// "^6<label>^7") is looked up without them; the translation is drawn
	// plain, which is what the theme draws those rows as anyway
	if( !strchr( text, '^' ))
		return text;

	char plain[LANG_OUT_SIZE];
	size_t n = 0;

	for( const char *s = text; *s && n + 1 < sizeof( plain ); s++ )
	{
		if( IsColorString( s ))
		{
			s++;
			continue;
		}
		plain[n++] = *s;
	}
	plain[n] = 0;

	if( !n || !strcmp( plain, text ))
		return text;

	out = Lang_Lookup( plain );
	return out ? out : text;
}

void UI_LangHold( bool hold )
{
	s_langHold += hold ? 1 : -1;

	if( s_langHold < 0 )
		s_langHold = 0;
}

/*
=================
UI_LangFrame

Loads the table of the pack cof_language names the first time and whenever
it changes. Returns true when the language changed after the first load, so
the caller re-lays out the open pages.
=================
*/
/*
=================
Lang_CollectGlyphs

Cry of Fear text beyond the fixed glyph ranges (FontManager.cpp uploads ASCII,
Latin-1, Latin Extended-A and Cyrillic): every other character the active
table's translations use, and every installed pack's display name - the
Language option lists them all in their own script ("简体中文"). Sorted, no
duplicates, with a hash the font atlases are named by.
=================
*/
#define LANG_MAX_GLYPHS	8192

static unsigned int s_langGlyphs[LANG_MAX_GLYPHS];
static int s_langNumGlyphs;
static unsigned int s_langGlyphHash;

static bool Lang_GlyphInFixedRanges( unsigned int cp )
{
	return cp < 0x180 || ( cp >= 0x400 && cp <= 0x45F );
}

static void Lang_AddGlyphs( const char *text )
{
	utfstate_t state;

	for( const char *p = text; p && *p && s_langNumGlyphs < LANG_MAX_GLYPHS; p++ )
	{
		unsigned int cp = state.Decode( (uint8_t)*p );

		if( cp && !Lang_GlyphInFixedRanges( cp ) && cp != 0xFFFD && cp != 0xFEFF )
			s_langGlyphs[s_langNumGlyphs++] = cp;
	}
}

static int Lang_GlyphCompare( const void *a, const void *b )
{
	unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static void Lang_CollectGlyphs( void )
{
	s_langNumGlyphs = 0;

	FOR_EACH_HASHMAP( s_langExact, i )
		Lang_AddGlyphs( s_langExact.Element( i ));

	// every pack's display name: the manifests are tiny
	int numFiles = 0;
	char **files = EngFuncs::GetFilesList( "languages/*", &numFiles, 1 );

	for( int i = 0; files && i < numFiles; i++ )
	{
		char path[160];
		int length = 0;
		const char *code = strrchr( files[i], '/' );

		code = code ? code + 1 : files[i];
		if( !code[0] || strpbrk( code, ".:\\/" ) || strlen( code ) > 60 )
			continue;

		snprintf( path, sizeof( path ), "languages/%s/manifest.txt", code );
		char *raw = (char *)EngFuncs::COM_LoadFile( path, &length );

		if( !raw )
			continue;

		char *text = new char[length + 1];
		memcpy( text, raw, length );
		text[length] = 0;
		EngFuncs::COM_FreeFile( raw );

		for( char *line = text; line && *line; )
		{
			char *next = strchr( line, '\n' );

			if( next )
				*next++ = 0;
			if( !strnicmp( line, "display_name=", 13 ))
				Lang_AddGlyphs( line + 13 );
			line = next;
		}

		delete[] text;
	}

	qsort( s_langGlyphs, s_langNumGlyphs, sizeof( s_langGlyphs[0] ), Lang_GlyphCompare );

	int n = 0;
	for( int i = 0; i < s_langNumGlyphs; i++ )
	{
		if( !n || s_langGlyphs[n - 1] != s_langGlyphs[i] )
			s_langGlyphs[n++] = s_langGlyphs[i];
	}
	s_langNumGlyphs = n;

	s_langGlyphHash = 2166136261u;
	for( int i = 0; i < s_langNumGlyphs; i++ )
		s_langGlyphHash = ( s_langGlyphHash ^ s_langGlyphs[i] ) * 16777619u;
	if( !s_langNumGlyphs )
		s_langGlyphHash = 0;
}

int UI_LangGlyphs( const unsigned int **out, unsigned int *hash )
{
	if( out )
		*out = s_langGlyphs;
	if( hash )
		*hash = s_langGlyphHash;
	return s_langNumGlyphs;
}

bool UI_LangFrame( void )
{
	const char *code = EngFuncs::GetCvarString( "cof_language" );

	if( !code )
		code = "";

	if( s_langLoaded && !strcmp( code, s_langCode ))
		return false;

	bool changed = s_langLoaded;

	Lang_Load( code );
	Lang_CollectGlyphs();
	return changed;
}

/*
=================
menu_cof_strings [reload]

Status of the menu string table; "reload" re-reads it from disk (for pack
authors editing menu-strings.tsv with the game running).
=================
*/
static void UI_LangStrings_f( void )
{
	if( EngFuncs::CmdArgc() > 1 && !stricmp( EngFuncs::CmdArgv( 1 ), "reload" ))
	{
		s_langLoaded = false;
		UI_LangFrame();
	}

	Con_Printf( "menu strings: cof_language \"%s\", %d strings, %d with conversions\n",
		s_langCode, s_langExact.Count(), s_langNumPatterns );

	if( EngFuncs::CmdArgc() > 1 && stricmp( EngFuncs::CmdArgv( 1 ), "reload" ))
	{
		const char *probe = EngFuncs::CmdArgv( 1 );
		Con_Printf( "menu strings: \"%s\" -> \"%s\"\n", probe, UI_LangText( probe ));
	}
}

ADD_COMMAND( menu_cof_strings, UI_LangStrings_f );
