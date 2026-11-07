/* 1.5: Cry of Fear text beyond one byte per glyph (engine/cof_text_logic.h). Prints what the Python side checks. */
#include <stdio.h>
#include <string.h>
#include "cof_text_logic.h"

static void Decode( const char *name, const char *s, int codepage, int tolerant )
{
	const unsigned char *p = (const unsigned char *)s;
	int len = (int)strlen( s );
	uint32_t cp;
	int n;

	printf( "%s", name );
	while(( n = CoF_Text_Next( p, len, codepage, tolerant, &cp )) > 0 )
	{
		printf( " %X/%d", (unsigned)cp, n );
		p += n;
		len -= n;
	}
	printf( "\n" );
}

static void Stream( const char *name, const char *s )
{
	cof_text_stream_t st;
	const unsigned char *p = (const unsigned char *)s;
	uint32_t cp;

	CoF_Text_StreamReset( &st );
	printf( "%s", name );
	for( ; *p; p++ )
	{
		if( CoF_Text_Feed( &st, *p, &cp ))
			printf( " %X", (unsigned)cp );
		else
			printf( " ." );
	}
	printf( "\n" );
}

int main( void )
{
	int b;

	Decode( "utf8", "A\xE4\xBD\xA0\xE5\xA5\xBD\xC3\xA9\xF0\x9F\x98\x80", COF_CP_UTF8, 0 );
	Decode( "utf8-bad", "\xE4\xBD\x41\xC0\xAF\xED\xA0\x80", COF_CP_UTF8, 0 );
	Decode( "cp1251", "\xCF\xF0\xE8\xE2\xE5\xF2", 1251, 0 );
	Decode( "cp1252-tolerant", "Caf\xC3\xA9 \xE4\xBD\xA0 \xE9t\xE9", 1252, 1 );
	Decode( "cp1252-strict", "Caf\xC3\xA9", 1252, 0 );
	Decode( "cp1250-tolerant", "\xD3\xB3", 1250, 1 );
	Stream( "stream", "a\xE4\xBD\xA0\xC3\xA9\xE4\xBD\x62" );
	printf( "cells" );
	for( b = 0; b < 256; b += 0x20 )
		printf( " %d", CoF_Text_Utf8ByteCell( b ));
	printf( "\n" );
	printf( "breaks %d %d %d %d %d %d\n", CoF_Text_BreakBetween( 0x4F60, 0x597D ), CoF_Text_BreakBetween( 'a', 'b' ),
		CoF_Text_BreakBetween( 0x4F60, 0x3002 ), CoF_Text_BreakBetween( 0x300C, 0x4F60 ),
		CoF_Text_BreakBetween( 'a', 0x4F60 ), CoF_Text_BreakBetween( 0x4F60, ',' ));
	printf( "to1252 %X %X %X %X\n", CoF_Text_To1252( 0x20AC ), CoF_Text_To1252( 0xE9 ), CoF_Text_To1252( 0x4F60 ),
		CoF_Text_To1252( 0x2014 ));
	printf( "cjk %d %d %d %d\n", CoF_Text_PreferCJK( 0x4F60, 0 ), CoF_Text_PreferCJK( 0x2026, 1 ),
		CoF_Text_PreferCJK( 0x2026, 0 ), CoF_Text_PreferCJK( 0x0416, 1 ));
	return 0;
}
