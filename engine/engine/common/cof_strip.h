/*
cof_strip.h - Cry of Fear story co-op: the engine's own message-strip lines although subtitles are off
GPL-3.0-or-later.

client.dll draws its message strip (the "ProFont" lines: dialogue subtitles, pickups, the engine's share messages and
quick-save lines) only while cl_subtitles is on (HUDControl_guess.cpp:1572 in the decompilation; client.dll 1003B744
reads it through the engine's pfnGetCvarFloat), and the menu's Subtitles option is that cvar. The server marks the
engine's own ProFont lines with two bytes after the text, which client.dll never reads (colour byte, string); while
one of them is on the strip, client.dll reads cl_subtitles as 1. The game's own lines keep the player's choice.
Research: docs/research/2026-10-18-cof-decompilation-survey.md section 4 (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef COF_STRIP_H
#define COF_STRIP_H

#if defined( _MSC_VER )
#define COF_STRIP_FN static __inline
#else
#define COF_STRIP_FN static inline
#endif

#define COF_STRIP_MARK0 0xC0
#define COF_STRIP_MARK1 0xF5
#define COF_STRIP_HOLD  8.0 // seconds: the strip's fade in, 4 s hold and fade out, with room to spare

// A ProFont payload for one of the engine's lines: colour, the text, the mark. Its length, or 0 when it does not fit.
COF_STRIP_FN int CoF_StripPayload( unsigned char *out, size_t size, int colour, const char *text )
{
	size_t length = strlen( text );
	if( length + 4 > size ) return 0;
	out[0] = (unsigned char)colour;
	memcpy( out + 1, text, length + 1 );
	out[length + 2] = COF_STRIP_MARK0;
	out[length + 3] = COF_STRIP_MARK1;
	return (int)length + 4;
}

// One of the engine's lines: the mark right after the text, and nothing more.
COF_STRIP_FN int CoF_StripOwnLine( const unsigned char *payload, int size )
{
	const unsigned char *end;
	if( !payload || size < 4 ) return 0;
	end = (const unsigned char *)memchr( payload + 1, 0, size - 1 );
	if( !end ) return 0;
	return ( end - payload ) + 3 == size && end[1] == COF_STRIP_MARK0 && end[2] == COF_STRIP_MARK1;
}

// What client.dll reads for a cvar: cl_subtitles is 1 while one of the engine's lines is on the strip (until).
COF_STRIP_FN float CoF_StripSubtitles( const char *name, float value, double now, double until )
{
	if( !name || strcmp( name, "cl_subtitles" ) || value != 0.0f || now >= until ) return value;
	return 1.0f;
}

#endif // COF_STRIP_H
