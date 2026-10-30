/*
CoFStoryCoopLogic.h -- story co-op from the main menu: what each Co-op page sends, with no engine in it

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

------------------------------------------------------------------------------

Cry of Fear Co-op 1.3.6 (the owner: "integrate the mod into the main menu"): the main menu's Co-op pages
(menus/CoFStoryCoop.cpp) start exactly the game the Cry of Fear Co-op app starts for the same choices
(tools/coop_play.py: STORY_SETTINGS, host_launch, join_launch, official_launch, party_size, parse_checkpoint,
network.parse_endpoint). The app writes those as two cfg files and a command line; a page writes the same lines
into the command buffer in one call, with "map" or "connect" last.

What differs, and why:
- cof_build and cof_label (the app's release, sent in the server's info reply) are not written here: the app sets
  them when it opens the game, and a page never changes them.
- cof_maps and cof_chapter_start are always written (empty when they do not apply): one game process can host
  more than once, and the last game's values would otherwise stay.
- The port is the one the game was started with (the app's -port).

tests/test_menu_coop.py compiles this header alone (tests/engine/cof_menu_coop_harness.cpp) and compares every
text with the app's own for the same choices. Plain C++ with no STL and no exceptions, as mainui builds.
*/
#ifndef COF_STORY_COOP_LOGIC_H
#define COF_STORY_COOP_LOGIC_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COF_STORY_NEW_GAME_MAP   "c_intro"
#define COF_STORY_MAX_COMPANIONS 3   // the game takes four players at most
#define COF_STORY_NAME_MAX       24  // coop_play.clean_name
#define COF_STORY_PASSWORD_MAX   64  // coop_play.clean_password
#define COF_STORY_DEFAULT_PORT   27015
#define COF_STORY_NAMES          32  // cof_session_data_t.players
#define COF_STORY_TEXT_MAX       4096
// coop_play.VERSION, the join check's version (cof_build): sent by a game hosted from the menu when the app did
// not set it (the game opened without the app), so the app's Join takes it
#define COF_STORY_BUILD          "1.1"

// coop_play.STORY_SETTINGS, in order: every game the app starts (story co-op on, the test tools off, the movement
// and network rates of 1.1.9)
static const char *const g_cofStorySettings[] =
{
	"cof_story_coop 1", "cof_story_trace 1", "sv_connect_timeout_ban 0", "sv_cheats 0", "sv_enttools_enable 0", "developer 0",
	"coop 0", "deathmatch 1", "mp_timelimit 0", "mp_fraglimit 0", "mp_weaponstay 0", "cof_test_gun 0", "fps_max 60",
	"rate 100000", "cl_cmdrate 100", "cl_updaterate 100", "sv_maxupdaterate 100", "ex_interp 0.1",
	"cl_forwardspeed 300", "cl_backspeed 200", "cl_sidespeed 200", "+mlook",
};
#define COF_STORY_NUM_SETTINGS ((int)( sizeof( g_cofStorySettings ) / sizeof( g_cofStorySettings[0] )))

struct cofStoryChapter_t
{
	const char *label;
	const char *map;
};

// "Start from": New game, then cofcoop.core.CHAPTERS without the intro (coop_play.CHAPTER_CHOICES)
static const cofStoryChapter_t g_cofStoryStarts[] =
{
	{ "New game (the intro; Enter skips cutscenes)", COF_STORY_NEW_GAME_MAP },
	{ "Camera prologue (skip accident cutscene)", "c_nightmare" },
	{ "Chapter 1 \xE2\x80\x94 Lost in a city", "c_start" },
	{ "Chapter 2 \xE2\x80\x94 Who is that doctor?", "c_basement" },
	{ "Chapter 3 \xE2\x80\x94 The city is not safe", "c_sewer3" },
	{ "Chapter 4 \xE2\x80\x94 Drowned in sorrow", "c_park3" },
	{ "Chapter 5 \xE2\x80\x94 Leaving this for good", "c_subwaysick2" },
	{ "Chapter 6 \xE2\x80\x94 It's not over yet", "c_bridge" },
	{ "Chapter 7 \xE2\x80\x94 Only safe at home", "c_attic" },
	{ "Chapter 8 \xE2\x80\x94 My life ends here (ending 1)", "c_ending1b" },
	{ "Chapter 8 \xE2\x80\x94 My life ends here (ending 2)", "c_ending2b" },
	{ "Chapter 8 \xE2\x80\x94 My life ends here (ending 3)", "c_ending3b" },
	{ "Ending 1", "c_ending1" },
	{ "Ending 2", "c_ending2" },
	{ "Ending 3", "c_ending3" },
	{ "Ending 4", "c_ending4" },
	{ "Ending 5", "c_ending5" },
};
#define COF_STORY_NUM_STARTS ((int)( sizeof( g_cofStoryStarts ) / sizeof( g_cofStoryStarts[0] )))

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------

struct cofStoryText_t
{
	char  *buf;
	size_t size;
	size_t len;
	bool   overflow;
};

static void CoFStory_TextInit( cofStoryText_t *t, char *buf, size_t size )
{
	t->buf = buf;
	t->size = size;
	t->len = 0;
	t->overflow = size == 0;
	if( size )
		buf[0] = 0;
}

static void CoFStory_TextAdd( cofStoryText_t *t, const char *fmt, ... )
{
	va_list args;
	int n;

	if( t->overflow )
		return;

	va_start( args, fmt );
	n = vsnprintf( t->buf + t->len, t->size - t->len, fmt, args );
	va_end( args );

	if( n < 0 || (size_t)n >= t->size - t->len )
	{
		t->overflow = true;
		t->buf[t->len] = 0;
		return;
	}
	t->len += (size_t)n;
}

// a bounded copy (snprintf "%s" without the format: compilers warn on its possible truncation)
static void CoFStory_Copy( char *dst, size_t size, const char *src )
{
	size_t n = 0;

	if( !size )
		return;
	while( src && src[n] && n + 1 < size )
	{
		dst[n] = src[n];
		n++;
	}
	dst[n] = 0;
}

// coop_play._clean as a filter for a text field: printable ASCII without " \ and ; (each would end the quoted
// argument or the command), spaces trimmed at both ends as clean_name does, at most limit characters.
static void CoFStory_Clean( char *out, size_t size, const char *in, size_t limit, bool trim )
{
	size_t n = 0;

	if( !size )
		return;

	for( ; in && *in && n + 1 < size && n < limit; in++ )
	{
		const unsigned char c = (unsigned char)*in;

		if( c < 32 || c > 126 || c == '"' || c == '\\' || c == ';' )
			continue;
		if( trim && c == ' ' && n == 0 )
			continue;
		out[n++] = (char)c;
	}
	while( trim && n > 0 && out[n - 1] == ' ' )
		n--;
	out[n] = 0;
}

// coop_play._clean: true when the app would take the field as typed (printable ASCII without " \ and ;, at most
// limit characters; trim: leading and trailing spaces do not count, as clean_name strips them). A page refuses the
// start otherwise, with the app's message (CoFStory_FieldMessage), rather than send something else than was typed.
static bool CoFStory_FieldValid( const char *in, size_t limit, bool trim, bool allowEmpty )
{
	size_t start = 0, end = in ? strlen( in ) : 0;

	if( trim )
	{
		while( start < end && in[start] == ' ' ) start++;
		while( end > start && in[end - 1] == ' ' ) end--;
	}
	if( end - start > limit || ( !allowEmpty && end == start ))
		return false;
	for( size_t i = start; i < end; i++ )
	{
		const unsigned char c = (unsigned char)in[i];
		if( c < 32 || c > 126 || c == '"' || c == '\\' || c == ';' )
			return false;
	}
	return true;
}

// the app's own refusal (coop_play._clean)
static void CoFStory_FieldMessage( char *out, size_t size, const char *label, size_t limit, bool empty )
{
	if( empty )
	{
		char lower[64];
		size_t n = 0;
		for( ; label[n] && n + 1 < sizeof( lower ); n++ )
			lower[n] = ( label[n] >= 'A' && label[n] <= 'Z' ) ? (char)( label[n] + 32 ) : label[n];
		lower[n] = 0;
		snprintf( out, size, "Enter %s.", lower );
	}
	else
		snprintf( out, size, "%s: use up to %d plain letters, digits and punctuation (no \", \\ or ;).", label, (int)limit );
}

static bool CoFStory_MapValid( const char *map )
{
	size_t n = 0;

	for( ; map && map[n]; n++ )
	{
		const char c = map[n];
		if( !(( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ))
			return false;
	}
	return n >= 1 && n <= 32;
}

// coop_play._SLOT
static bool CoFStory_SlotValid( const char *slot )
{
	size_t n = 0;

	for( ; slot && slot[n]; n++ )
	{
		const char c = slot[n];
		if( !(( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ))
			return false;
	}
	return n >= 1 && n <= 24;
}

// coop_play._TAG: a merged-maps tag, 8 hex digits
static bool CoFStory_TagValid( const char *tag )
{
	size_t n = 0;

	for( ; tag && tag[n]; n++ )
	{
		const char c = tag[n];
		if( !(( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' )))
			return false;
	}
	return n == 8;
}

// ---------------------------------------------------------------------------
// a start: Host co-op, Play with AI, Continue co-op
// ---------------------------------------------------------------------------

// coop_play.party_size: (places, AI companions). Play with AI: you and 1 to 3 AIs. Host: the chosen places, and
// with fill an AI in every place a friend has not taken. Continue keeps the checkpoint's places (checkpointPlayers,
// 0 for a new game or a chapter), which the engine requires.
static void CoFStory_PartySize( bool solo, int players, int companions, bool fill, int checkpointPlayers,
	int *places, int *ais )
{
	if( solo )
	{
		int a = companions;
		if( a > COF_STORY_MAX_COMPANIONS ) a = COF_STORY_MAX_COMPANIONS;
		if( a < 1 ) a = 1;
		*places = checkpointPlayers ? checkpointPlayers : a + 1;
		if( a > *places - 1 ) a = *places - 1;
		*ais = a < 1 ? 1 : a;
		return;
	}
	*places = checkpointPlayers ? checkpointPlayers : players;
	*ais = fill ? *places - 1 : 0;
}

struct cofStoryStart_t
{
	bool        solo;       // Play with AI: LAN only, no password
	bool        cont;       // Continue: the checkpoint slot below
	const char *name;       // already cleaned (CoFStory_Clean)
	const char *password;   // already cleaned; ignored when solo
	const char *map;        // the start map, or the checkpoint's map
	const char *slot;       // the checkpoint's slot ("" with cont: the latest)
	const char *maps;       // this copy's merged-maps tag (the app's cof_menu_maps), or ""
	int         places;     // maxplayers, from CoFStory_PartySize
	int         companions; // AI companions, from CoFStory_PartySize
	bool        bonusRoom;  // the bonus room open for everyone; unticked, as far as the host has unlocked it (1.3.8)
	bool        merged;     // merged story maps; Continue passes the checkpoint's own
	bool        flashlight; // the co-op flashlight on F
};

static void CoFStory_AddSettings( cofStoryText_t *t )
{
	for( int i = 0; i < COF_STORY_NUM_SETTINGS; i++ )
		CoFStory_TextAdd( t, "%s\n", g_cofStorySettings[i] );
}

/*
The command text of a start (coop_play.host_launch): the story settings, the session lines, "map" last.
false, with out empty, when a value is one host_launch refuses.
*/
static bool CoFStory_StartCommands( char *out, size_t size, const cofStoryStart_t *s )
{
	cofStoryText_t t;
	const char *password = s->solo ? "" : ( s->password ? s->password : "" );
	const bool chapter = !s->cont && strcmp( s->map, COF_STORY_NEW_GAME_MAP );

	CoFStory_TextInit( &t, out, size );

	if( !s->name || !s->name[0] || !CoFStory_MapValid( s->map ) || s->places < 2 || s->places > 32 ||
		s->companions < ( s->solo ? 1 : 0 ) || s->companions > s->places - 1 ||
		( s->slot && s->slot[0] && ( !s->cont || !CoFStory_SlotValid( s->slot ))))
	{
		if( size ) out[0] = 0;
		return false;
	}

	CoFStory_AddSettings( &t );
	CoFStory_TextAdd( &t, "name \"%s\"\n", s->name );
	CoFStory_TextAdd( &t, "maxplayers %d\n", s->places );
	CoFStory_TextAdd( &t, "sv_lan %d\n", s->solo ? 1 : 0 );
	CoFStory_TextAdd( &t, "sv_password \"%s\"\n", password );
	CoFStory_TextAdd( &t, "password \"%s\"\n", password );
	CoFStory_TextAdd( &t, "hostname \"%s's game\"\n", s->name );
	CoFStory_TextAdd( &t, "cof_companion %d\n", s->companions );
	CoFStory_TextAdd( &t, "cof_companion_drive 0\n" );
	CoFStory_TextAdd( &t, "cof_continue %d\n", s->cont ? 1 : 0 );
	// 1.3.8: unticked, the host's own unlocks decide (-1: scriptsettings.dat, which the app brings up to date from
	// the player's own Cry of Fear); it was 0, shut whatever they had unlocked
	CoFStory_TextAdd( &t, "cof_bonus_room %d\n", s->bonusRoom ? 1 : -1 );
	CoFStory_TextAdd( &t, "cof_merged_maps %d\n", s->merged ? 1 : 0 );
	CoFStory_TextAdd( &t, "cof_coop_flashlight %d\n", s->flashlight ? 1 : 0 );
	CoFStory_TextAdd( &t, "cof_continue_slot \"%s\"\n", s->slot ? s->slot : "" );
	CoFStory_TextAdd( &t, "cof_maps \"%s\"\n", s->merged && CoFStory_TagValid( s->maps ) ? s->maps : "" );
	// a chapter start: the engine hands out what the story would have given by then (1.1.8)
	CoFStory_TextAdd( &t, "cof_chapter_start \"%s\"\n", chapter ? s->map : "" );
	CoFStory_TextAdd( &t, "map %s\n", s->map );

	if( t.overflow )
	{
		if( size ) out[0] = 0;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Cry of Fear's own co-op (coop_play.official_launch, plus the lobby options of Enhanced's Host co-op page)
// ---------------------------------------------------------------------------

struct cofStoryOfficial_t
{
	const char *name;       // already cleaned
	const char *hostname;   // already cleaned
	const char *password;   // already cleaned
	const char *map;        // a cof_ / c_cof_ entry map
	int         players;    // 2 to 4
	int         lan;        // sv_lan
	int         autostart;  // mp_footsteps: the lobby's autostart seconds
	int         respawn;    // sv_auto_respawn_time
	int         difficulty; // 1 to 4
};

static bool CoFStory_OfficialCommands( char *out, size_t size, const cofStoryOfficial_t *o )
{
	cofStoryText_t t;

	CoFStory_TextInit( &t, out, size );

	if( !o->name || !o->name[0] || !CoFStory_MapValid( o->map ) || o->players < 2 || o->players > 4 ||
		( strncmp( o->map, "cof_", 4 ) && strncmp( o->map, "c_cof_", 6 )))
	{
		if( size ) out[0] = 0;
		return false;
	}

	CoFStory_AddSettings( &t );
	CoFStory_TextAdd( &t, "name \"%s\"\n", o->name );
	CoFStory_TextAdd( &t, "maxplayers %d\n", o->players );
	CoFStory_TextAdd( &t, "sv_lan %d\n", o->lan ? 1 : 0 );
	CoFStory_TextAdd( &t, "sv_password \"%s\"\n", o->password ? o->password : "" );
	CoFStory_TextAdd( &t, "password \"%s\"\n", o->password ? o->password : "" );
	CoFStory_TextAdd( &t, "hostname \"%s\"\n", o->hostname && o->hostname[0] ? o->hostname : "Cry of Fear co-op" );
	// the story layer, the AI companions, merged maps and Continue off; the game's multiplayer rules
	CoFStory_TextAdd( &t, "cof_story_coop 0\ncoop 0\ndeathmatch 1\ncof_companion 0\ncof_companion_drive 0\n"
		"cof_merged_maps 0\ncof_continue 0\ncof_continue_slot \"\"\ncof_bonus_room 0\ncof_maps \"\"\n"
		"cof_chapter_start \"\"\n" );
	CoFStory_TextAdd( &t, "mp_footsteps %d\nsv_auto_respawn_time %d\npausable 0\ndifficulty %d\n",
		o->autostart, o->respawn, o->difficulty );
	CoFStory_TextAdd( &t, "map %s\n", o->map );

	if( t.overflow )
	{
		if( size ) out[0] = 0;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Join co-op: an address or a join code (cofcoop/network.py parse_endpoint, make_join_code)
// ---------------------------------------------------------------------------

static uint32_t CoFStory_Crc32( const unsigned char *data, size_t len )
{
	uint32_t crc = 0xFFFFFFFFu;

	for( size_t i = 0; i < len; i++ )
	{
		crc ^= data[i];
		for( int k = 0; k < 8; k++ )
			crc = ( crc >> 1 ) ^ ( 0xEDB88320u & ( 0u - ( crc & 1u )));
	}
	return crc ^ 0xFFFFFFFFu;
}

// RFC 4648 base32 without its padding, as Python's b32decode takes it once the padding is put back: a length
// that padding cannot complete (1, 3 or 6 characters into the last group of 8) is refused, the last group's
// leftover bits are dropped
static bool CoFStory_Base32( const char *in, size_t inLen, unsigned char *out, size_t outSize, size_t *outLen )
{
	uint32_t acc = 0;
	int bits = 0;
	size_t n = 0;
	const size_t rest = inLen % 8;

	if( rest == 1 || rest == 3 || rest == 6 )
		return false;

	for( size_t i = 0; i < inLen; i++ )
	{
		const char c = in[i];
		int v;

		if( c >= 'A' && c <= 'Z' ) v = c - 'A';
		else if( c >= '2' && c <= '7' ) v = c - '2' + 26;
		else return false;

		acc = ( acc << 5 ) | (uint32_t)v;
		bits += 5;
		if( bits >= 8 )
		{
			bits -= 8;
			if( n >= outSize )
				return false;
			out[n++] = (unsigned char)(( acc >> bits ) & 0xFF );
		}
		acc &= ( 1u << bits ) - 1u;
	}
	*outLen = n;
	return true;
}

// network.Endpoint: every dot-separated label [a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?, 253 characters at most
static bool CoFStory_HostLabelsValid( const char *host )
{
	const size_t len = strlen( host );
	size_t label = 0;

	if( !len || len > 253 )
		return false;

	for( size_t i = 0; i <= len; i++ )
	{
		const char c = host[i];

		if( c == '.' || c == 0 )
		{
			if( label == 0 || label > 63 || host[i - label] == '-' || host[i - 1] == '-' )
				return false;
			label = 0;
			continue;
		}
		if( !(( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '-' ))
			return false;
		label++;
	}
	return true;
}

// Python's ipaddress.IPv4Address: four parts 0 to 255, no leading zeros
static bool CoFStory_IPv4Valid( const char *host )
{
	int parts = 0;
	const char *p = host;

	while( *p )
	{
		int value = 0, digits = 0;
		const char *start = p;

		while( *p >= '0' && *p <= '9' )
		{
			value = value * 10 + ( *p - '0' );
			digits++;
			p++;
			if( digits > 3 )
				return false;
		}
		if( digits == 0 || value > 255 || ( digits > 1 && *start == '0' ))
			return false;
		parts++;
		if( *p == '.' )
		{
			p++;
			if( !*p )
				return false;
		}
		else if( *p )
			return false;
	}
	return parts == 4;
}

static bool CoFStory_IsJoinCode( const char *value )
{
	return ( value[0] == 'C' || value[0] == 'c' ) && ( value[1] == 'O' || value[1] == 'o' ) &&
		( value[2] == 'F' || value[2] == 'f' ) && value[3] == '1' && value[4] == '-';
}

/*
An address (IPv4 or host name, optionally :port) or a COF1 join code, as the app's Join takes them
(network.parse_endpoint). host gets the lower-case host without a trailing dot. false, with error set to the app's
own message, otherwise.
*/
static bool CoFStory_ParseEndpoint( const char *text, char *host, size_t hostSize, int *port, const char **error )
{
	static const char *damaged = "Join code is damaged or incomplete; copy it again.";
	static const char *general = "Enter an IPv4 address or DNS hostname, optionally followed by :port.";
	char value[608];
	size_t len;
	const char *start = text ? text : "";

	*error = "";
	while( *start == ' ' || *start == '\t' || *start == '\n' || *start == '\r' )
		start++;
	len = strlen( start );
	while( len && ( start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' || start[len - 1] == '\r' ))
		len--;
	if( len > 600 )
	{
		*error = len >= 5 && CoFStory_IsJoinCode( start ) ? "Join code is too long." : general;
		return false;
	}
	memcpy( value, start, len );
	value[len] = 0;

	if( len >= 5 && CoFStory_IsJoinCode( value ))
	{
		unsigned char payload[400];
		size_t payloadLen = 0;
		char upper[608];
		char *body, *check;
		char expected[9];

		for( size_t i = 0; i <= len; i++ )
			upper[i] = ( value[i] >= 'a' && value[i] <= 'z' ) ? (char)( value[i] - 32 ) : value[i];

		// exactly three parts: COF1, the base32 body, 8 hex digits of its CRC-32
		body = upper + 5;
		check = strchr( body, '-' );
		if( !check || strchr( check + 1, '-' ))
		{
			*error = damaged;
			return false;
		}
		*check++ = 0;
		if( strlen( check ) != 8 || !CoFStory_Base32( body, strlen( body ), payload, sizeof( payload ) - 1, &payloadLen ))
		{
			*error = damaged;
			return false;
		}
		snprintf( expected, sizeof( expected ), "%08X", (unsigned)CoFStory_Crc32( payload, payloadLen ));
		if( strcmp( expected, check ))
		{
			*error = damaged;
			return false;
		}
		for( size_t i = 0; i < payloadLen; i++ )
		{
			if( payload[i] > 127 )
			{
				*error = damaged;
				return false;
			}
		}
		memcpy( value, payload, payloadLen );
		value[payloadLen] = 0;
	}

	int colons = 0;
	for( const char *p = value; *p; p++ )
		if( *p == ':' ) colons++;
	if( colons > 1 )
	{
		*error = "Cry of Fear requires an IPv4 address or hostname, not IPv6.";
		return false;
	}

	char *sep = strchr( value, ':' );
	int p = COF_STORY_DEFAULT_PORT;

	if( sep )
	{
		const char *digits = sep + 1;
		const size_t n = strlen( digits );

		*sep = 0;
		if( n < 1 || n > 5 || strspn( digits, "0123456789" ) != n )
		{
			*error = "The port must be a number between 1 and 65535.";
			return false;
		}
		p = 0;
		for( size_t i = 0; i < n; i++ )
			p = p * 10 + ( digits[i] - '0' );
	}

	// Endpoint: the host lower-cased, trailing dots dropped; the labels, then an all-digit host as IPv4, then the port
	size_t h = strlen( value );
	for( size_t i = 0; i < h; i++ )
		if( value[i] >= 'A' && value[i] <= 'Z' ) value[i] = (char)( value[i] + 32 );
	while( h && value[h - 1] == '.' )
		value[--h] = 0;

	if( !CoFStory_HostLabelsValid( value ))
	{
		*error = general;
		return false;
	}
	if( strspn( value, "0123456789." ) == h && !CoFStory_IPv4Valid( value ))
	{
		*error = "Invalid IPv4 address.";
		return false;
	}
	if( p < 1 || p > 65535 )
	{
		*error = "The port must be between 1 and 65535.";
		return false;
	}
	if( h + 1 > hostSize )
	{
		*error = general;
		return false;
	}
	memcpy( host, value, h + 1 );
	*port = p;
	return true;
}

/*
The command text of a join (coop_play.join_launch): the story settings, name and password, "connect" last.
*/
static bool CoFStory_JoinCommands( char *out, size_t size, const char *name, const char *password, const char *host, int port )
{
	cofStoryText_t t;

	CoFStory_TextInit( &t, out, size );
	if( !name || !name[0] || !host || !host[0] )
	{
		if( size ) out[0] = 0;
		return false;
	}
	CoFStory_AddSettings( &t );
	CoFStory_TextAdd( &t, "name \"%s\"\n", name );
	CoFStory_TextAdd( &t, "password \"%s\"\n", password ? password : "" );
	CoFStory_TextAdd( &t, "connect %s:%d\n", host, port );

	if( t.overflow )
	{
		if( size ) out[0] = 0;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Continue co-op: the checkpoints (coop_play.parse_checkpoint, parse_merged_record, slot_title, continue_warning)
// ---------------------------------------------------------------------------

struct cofStoryCheckpoint_t
{
	char slot[25];
	char map[33];
	char section[33];   // the original map the first player was in, in a merged map ("" otherwise)
	int  players;
	bool merged;
	int  numNames;
	char names[COF_STORY_NAMES][33];
};

static uint32_t CoFStory_U32( const unsigned char *p )
{
	return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 );
}

// merged.txt of a checkpoint: "merged 0|1" and "section <map>", nothing else
static void CoFStory_ParseMerged( const unsigned char *data, size_t len, bool *merged, char *section, size_t sectionSize )
{
	char text[256];
	char seenMerged = 0, seenSection = 0;
	bool m = false;
	char sec[64] = "";
	size_t i, n = len < sizeof( text ) - 1 ? len : sizeof( text ) - 1;

	*merged = false;
	section[0] = 0;
	if( len >= sizeof( text ))
		return;
	for( i = 0; i < n; i++ )
	{
		if( data[i] > 127 )
			return;
		text[i] = (char)data[i];
	}
	text[n] = 0;

	char *line = text;
	while( line && *line )
	{
		char *next = strpbrk( line, "\r\n" );
		char key[64], val[64], rest[8];
		int fields;

		if( next )
			*next++ = 0;
		while( line[0] == ' ' || line[0] == '\t' )
			line++;
		if( !line[0] )
		{
			line = next;
			continue;
		}
		key[0] = val[0] = rest[0] = 0;
		fields = sscanf( line, "%63s %63s %7s", key, val, rest );
		if( fields != 2 )
			return;
		if( !strcmp( key, "merged" ) && !seenMerged )
		{
			if( strcmp( val, "0" ) && strcmp( val, "1" ))
				return;
			m = val[0] == '1';
			seenMerged = 1;
		}
		else if( !strcmp( key, "section" ) && !seenSection )
		{
			if( !CoFStory_MapValid( val ))
				return;
			strcpy( sec, val );
			seenSection = 1;
		}
		else
			return;
		line = next;
	}
	if( !seenMerged || !seenSection )
		return;
	*merged = m;
	CoFStory_Copy( section, sectionSize, sec );
}

/*
A co-op checkpoint archive (cof_archive_header_t, format 2 session.dat), or false. The layout is the engine's
sv_cof_session_data.h: header magic 'CFCS', version 1, entry count, total size, checksum; then entries of a 64-byte
name and a size; session.dat holds version 2, the player limit, the map (64 bytes at 16) and 32 names of 32 bytes
at 16 + 64 + 256 * 64.
*/
static bool CoFStory_ParseCheckpoint( const char *slot, const unsigned char *data, size_t len, cofStoryCheckpoint_t *out )
{
	const size_t header = 20, entryHead = 68, namesAt = 16 + 64 + 256 * 64;
	size_t pos;
	uint32_t count;
	bool found = false;

	memset( out, 0, sizeof( *out ));
	if( !CoFStory_SlotValid( slot ) || len < header )
		return false;
	if( CoFStory_U32( data ) != 0x53434643u || CoFStory_U32( data + 4 ) != 1 || CoFStory_U32( data + 12 ) != len )
		return false;
	count = CoFStory_U32( data + 8 );
	if( count == 0 || count > 1024 )
		return false;

	CoFStory_Copy( out->slot, sizeof( out->slot ), slot );
	pos = header;
	for( uint32_t e = 0; e < count; e++ )
	{
		char entry[65];
		uint32_t length;

		if( pos + entryHead > len )
			return false;
		memcpy( entry, data + pos, 64 );
		entry[64] = 0;
		for( char *c = entry; *c; c++ )
			if( *c >= 'A' && *c <= 'Z' ) *c = (char)( *c + 32 );
		length = CoFStory_U32( data + pos + 64 );
		pos += entryHead;
		if( length > len - pos )
			return false;

		if( !strcmp( entry, "session.dat" ) && !found )
		{
			const unsigned char *body = data + pos;
			char map[65];

			if( length < namesAt + 32 * 32 )
				return false;
			if( CoFStory_U32( body ) != 2 )
				return false;
			out->players = (int)CoFStory_U32( body + 4 );
			memcpy( map, body + 16, 64 );
			map[64] = 0;
			for( char *c = map; *c; c++ )
				if( *c >= 'A' && *c <= 'Z' ) *c = (char)( *c + 32 );
			if( out->players < 2 || out->players > 32 || !CoFStory_MapValid( map ))
				return false;
			CoFStory_Copy( out->map, sizeof( out->map ), map );
			for( int i = 0; i < 32; i++ )
			{
				char name[33];
				memcpy( name, body + namesAt + i * 32, 32 );
				name[32] = 0;
				if( name[0] )
					CoFStory_Copy( out->names[out->numNames++], sizeof( out->names[0] ), name );
			}
			found = true;
		}
		else if( !strcmp( entry, "merged.txt" ))
			CoFStory_ParseMerged( data + pos, length, &out->merged, out->section, sizeof( out->section ));
		pos += length;
	}
	return found;
}

// coop_play.slot_title
static void CoFStory_SlotTitle( const char *slot, char *out, size_t size )
{
	if( !strcmp( slot, "autosave" ))
		snprintf( out, size, "Last level change" );
	else if( !strncmp( slot, "tape", 4 ) && slot[4] >= '1' && slot[4] <= '5' && !slot[5] )
		snprintf( out, size, "Tape recorder save %c", slot[4] );
	else
		snprintf( out, size, "Saved as %s", slot );
}

static bool CoFStory_NameEqual( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
	{
		char x = *a, y = *b;
		if( x >= 'A' && x <= 'Z' ) x = (char)( x + 32 );
		if( y >= 'A' && y <= 'Z' ) y = (char)( y + 32 );
		if( x != y )
			return false;
	}
	return *a == *b;
}

// coop_play.continue_warning: items come back only under the name they were saved with; false when none is needed
static bool CoFStory_ContinueWarning( const cofStoryCheckpoint_t *c, const char *name, char *out, size_t size )
{
	char trimmed[64];
	cofStoryText_t t;

	CoFStory_Clean( trimmed, sizeof( trimmed ), name, sizeof( trimmed ) - 1, true );
	if( !c->numNames )
		return false;
	for( int i = 0; i < c->numNames; i++ )
		if( CoFStory_NameEqual( trimmed, c->names[i] ))
			return false;

	CoFStory_TextInit( &t, out, size );
	CoFStory_TextAdd( &t, "This checkpoint was saved by " );
	for( int i = 0; i < c->numNames; i++ )
		CoFStory_TextAdd( &t, "%s%s", i ? ", " : "", c->names[i] );
	CoFStory_TextAdd( &t, ". Items come back only under the same name: change Your name to one of those, or "
		"continue without your items." );
	return true;
}

// ---------------------------------------------------------------------------
// the app's invite file (cryoffear/cof_invite.txt): the join code, the Internet and LAN addresses it shows
// ---------------------------------------------------------------------------

struct cofStoryInvite_t
{
	char code[128];
	char address[96];
	char lan[96];
};

static void CoFStory_ParseInvite( const char *text, size_t len, cofStoryInvite_t *out )
{
	char line[256];
	size_t i = 0;

	memset( out, 0, sizeof( *out ));
	while( i < len )
	{
		size_t n = 0;

		while( i < len && text[i] != '\n' && text[i] != '\r' )
		{
			if( n + 1 < sizeof( line ))
				line[n++] = text[i];
			i++;
		}
		while( i < len && ( text[i] == '\n' || text[i] == '\r' ))
			i++;
		line[n] = 0;

		char *space = strchr( line, ' ' );
		if( !space )
			continue;
		*space++ = 0;

		// the value is shown and nothing else: printable ASCII only
		bool plain = space[0] != 0;
		for( char *c = space; *c; c++ )
			if( (unsigned char)*c < 33 || (unsigned char)*c > 126 ) plain = false;
		if( !plain )
			continue;

		if( !strcmp( line, "code" ))
			CoFStory_Copy( out->code, sizeof( out->code ), space );
		else if( !strcmp( line, "address" ))
			CoFStory_Copy( out->address, sizeof( out->address ), space );
		else if( !strcmp( line, "lan" ))
			CoFStory_Copy( out->lan, sizeof( out->lan ), space );
	}
}

#endif // COF_STORY_COOP_LOGIC_H
