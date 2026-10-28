/* 1.3.6: the main menu's Co-op pages (mainui menus/CoFStoryCoopLogic.h), driven by tests/test_menu_coop.py.
 * cases.txt: one case a line, tab-separated, every field hex-encoded (the first: what to run). Each answer is one
 * line: the case number, a tab, the answer hex-encoded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logic_under_test.h"

static char g_fields[32][90000]; // a checkpoint archive comes in as hex text, hex-encoded again
static int g_count;

static int Unhex( char c )
{
	if( c >= '0' && c <= '9' ) return c - '0';
	if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

static void Split( char *line )
{
	g_count = 0;
	for( int i = 0; i < 32; i++ )
		g_fields[i][0] = 0; // an empty field is an empty hex string: nothing of the last case stays
	char *p = line;
	while( *p && g_count < 32 )
	{
		char *end = strchr( p, '\t' );
		size_t n = 0;
		if( end ) *end = 0;
		for( size_t i = 0; p[i] && p[i + 1] && n + 1 < sizeof( g_fields[0] ); i += 2 )
			g_fields[g_count][n++] = (char)( Unhex( p[i] ) * 16 + Unhex( p[i + 1] ));
		g_fields[g_count][n] = 0;
		g_count++;
		if( !end ) break;
		p = end + 1;
	}
}

static void Answer( int id, const char *text )
{
	printf( "%d\t", id );
	for( const unsigned char *p = (const unsigned char *)text; *p; p++ )
		printf( "%02x", *p );
	printf( "\n" );
}

static int Int( int i ) { return atoi( g_fields[i] ); }
static bool Flag( int i ) { return g_fields[i][0] == '1'; }

int main( void )
{
	FILE *f = fopen( "cases.txt", "rb" );
	static char line[1 << 19];
	static char out[COF_STORY_TEXT_MAX];
	int id = 0;

	if( !f )
	{
		puts( "no cases.txt" );
		return 1;
	}
	while( fgets( line, sizeof( line ), f ))
	{
		line[strcspn( line, "\r\n" )] = 0;
		Split( line );
		const char *what = g_fields[0];

		if( !strcmp( what, "settings" ))
		{
			cofStoryText_t t;
			CoFStory_TextInit( &t, out, sizeof( out ));
			CoFStory_AddSettings( &t );
			Answer( id, out );
		}
		else if( !strcmp( what, "starts" ))
		{
			cofStoryText_t t;
			CoFStory_TextInit( &t, out, sizeof( out ));
			for( int i = 0; i < COF_STORY_NUM_STARTS; i++ )
				CoFStory_TextAdd( &t, "%s\t%s\n", g_cofStoryStarts[i].label, g_cofStoryStarts[i].map );
			Answer( id, out );
		}
		else if( !strcmp( what, "party" ))
		{
			int places = -1, ais = -1;
			CoFStory_PartySize( Flag( 1 ), Int( 2 ), Int( 3 ), Flag( 4 ), Int( 5 ), &places, &ais );
			snprintf( out, sizeof( out ), "%d %d", places, ais );
			Answer( id, out );
		}
		else if( !strcmp( what, "start" ))
		{
			cofStoryStart_t s;
			s.solo = Flag( 1 ); s.cont = Flag( 2 ); s.name = g_fields[3]; s.password = g_fields[4]; s.map = g_fields[5];
			s.slot = g_fields[6]; s.maps = g_fields[7]; s.places = Int( 8 ); s.companions = Int( 9 );
			s.bonusRoom = Flag( 10 ); s.merged = Flag( 11 ); s.flashlight = Flag( 12 );
			const bool ok = CoFStory_StartCommands( out, sizeof( out ), &s );
			Answer( id, ok ? out : "REFUSED" );
		}
		else if( !strcmp( what, "official" ))
		{
			cofStoryOfficial_t o;
			o.name = g_fields[1]; o.hostname = g_fields[2]; o.password = g_fields[3]; o.map = g_fields[4];
			o.players = Int( 5 ); o.lan = Int( 6 ); o.autostart = Int( 7 ); o.respawn = Int( 8 ); o.difficulty = Int( 9 );
			const bool ok = CoFStory_OfficialCommands( out, sizeof( out ), &o );
			Answer( id, ok ? out : "REFUSED" );
		}
		else if( !strcmp( what, "endpoint" ))
		{
			char host[256];
			int port = 0;
			const char *error = "";
			if( CoFStory_ParseEndpoint( g_fields[1], host, sizeof( host ), &port, &error ))
				snprintf( out, sizeof( out ), "ok %s:%d", host, port );
			else
				snprintf( out, sizeof( out ), "error %s", error );
			Answer( id, out );
		}
		else if( !strcmp( what, "join" ))
		{
			const bool ok = CoFStory_JoinCommands( out, sizeof( out ), g_fields[1], g_fields[2], g_fields[3], Int( 4 ));
			Answer( id, ok ? out : "REFUSED" );
		}
		else if( !strcmp( what, "clean" ))
		{
			char cleaned[256];
			CoFStory_Clean( cleaned, sizeof( cleaned ), g_fields[1], (size_t)Int( 2 ), Flag( 3 ));
			Answer( id, cleaned );
		}
		else if( !strcmp( what, "field" ))
		{
			// value, limit, trim, allow empty, label: "ok", or the app's message
			const char *v = g_fields[1];
			size_t s = 0, e = strlen( v );
			if( Flag( 3 ))
			{
				while( s < e && v[s] == ' ' ) s++;
				while( e > s && v[e - 1] == ' ' ) e--;
			}
			if( CoFStory_FieldValid( v, (size_t)Int( 2 ), Flag( 3 ), Flag( 4 )))
				Answer( id, "ok" );
			else
			{
				CoFStory_FieldMessage( out, sizeof( out ), g_fields[5], (size_t)Int( 2 ), e == s && e - s <= (size_t)Int( 2 ));
				Answer( id, out );
			}
		}
		else if( !strcmp( what, "build" ))
			Answer( id, COF_STORY_BUILD );
		else if( !strcmp( what, "checkpoint" ))
		{
			// field 2: the archive's bytes as hex text (it is hex-encoded once more, as every field: raw bytes hold zeros)
			static unsigned char bytes[1 << 15];
			const char *hex = g_fields[2];
			size_t n = 0;
			for( size_t i = 0; hex[i] && hex[i + 1] && n < sizeof( bytes ); i += 2 )
				bytes[n++] = (unsigned char)( Unhex( hex[i] ) * 16 + Unhex( hex[i + 1] ));
			cofStoryCheckpoint_t c;
			if( !CoFStory_ParseCheckpoint( g_fields[1], bytes, n, &c ))
				Answer( id, "NONE" );
			else
			{
				cofStoryText_t t;
				CoFStory_TextInit( &t, out, sizeof( out ));
				CoFStory_TextAdd( &t, "%s|%s|%d|%s|%d|%s", c.slot, c.map, c.players, c.section, c.merged ? 1 : 0, "" );
				for( int i = 0; i < c.numNames; i++ )
					CoFStory_TextAdd( &t, "%s%s", i ? "," : "", c.names[i] );
				Answer( id, out );
			}
		}
		else if( !strcmp( what, "slot" ))
		{
			char title[96];
			CoFStory_SlotTitle( g_fields[1], title, sizeof( title ));
			Answer( id, title );
		}
		else if( !strcmp( what, "warning" ))
		{
			cofStoryCheckpoint_t c;
			memset( &c, 0, sizeof( c ));
			// field 2: the names, comma-separated
			char *names = g_fields[2];
			while( names && *names && c.numNames < COF_STORY_NAMES )
			{
				char *comma = strchr( names, ',' );
				if( comma ) *comma = 0;
				CoFStory_Copy( c.names[c.numNames++], sizeof( c.names[0] ), names );
				names = comma ? comma + 1 : NULL;
			}
			Answer( id, CoFStory_ContinueWarning( &c, g_fields[1], out, sizeof( out )) ? out : "NONE" );
		}
		else if( !strcmp( what, "invite" ))
		{
			cofStoryInvite_t inv;
			CoFStory_ParseInvite( g_fields[1], strlen( g_fields[1] ), &inv );
			snprintf( out, sizeof( out ), "%s|%s|%s", inv.code, inv.address, inv.lan );
			Answer( id, out );
		}
		else
			Answer( id, "UNKNOWN" );
		id++;
	}
	fclose( f );
	fprintf( stderr, "menu co-op cases: %d\n", id );
	return 0;
}
