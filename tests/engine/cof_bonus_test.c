/* The bonus room switch (sv_cof_bonus.h): scriptsettings.dat as the game reads it.
 * The reader below is the retail CheckingEndingOrUnlockables (hl.dll 0x100DD000):
 * strtok( buf, "\n" ), non-empty tokens counted from 1, then strstr( token, LOCKED )
 * is false, strstr( token, UNLOCKED ) true, neither false. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bonus_under_test.h"
#include "bonus_fixture_paths.h"

/* -1 no such line, 0 locked (or unreadable), 1 unlocked */
static int Read( const unsigned char *text, size_t size, int line, const char *locked, const char *unlocked )
{
	char *copy = malloc( size + 1 ), *token;
	int n = 0, result = -1;
	memcpy( copy, text, size ); copy[size] = 0;
	for( token = strtok( copy, "\n" ); token; token = strtok( NULL, "\n" ))
		if( ++n == line ) { result = strstr( token, locked ) ? 0 : strstr( token, unlocked ) ? 1 : 0; break; }
	free( copy );
	return result;
}

static int All( const unsigned char *text, size_t size, int want )
{
	int i;
	for( i = 0; i < COF_BONUS_LINES; i++ )
		if( Read( text, size, cof_bonus_lines[i].line, cof_bonus_lines[i].locked, cof_bonus_lines[i].unlocked ) != want ) return 0;
	return 1;
}

static int Gated( int line )
{
	int i;
	for( i = 0; i < COF_BONUS_LINES; i++ ) if( cof_bonus_lines[i].line == line ) return 1;
	return 0;
}

/* every line the switch does not own is byte for byte what it was */
static void Untouched( const unsigned char *before, const unsigned char *after, size_t size )
{
	size_t i = 0;
	int line = 0;
	while( i < size )
	{
		size_t start;
		while( i < size && before[i] == '\n' ) { assert( after[i] == '\n' ); i++; }
		if( i >= size ) break;
		start = i; line++;
		while( i < size && before[i] != '\n' ) i++;
		if( !Gated( line )) assert( !memcmp( before + start, after + start, i - start ));
	}
}

static unsigned char *Load( const char *path, size_t *size )
{
	FILE *f = fopen( path, "rb" );
	unsigned char *data;
	long length;
	if( !f ) return NULL;
	fseek( f, 0, SEEK_END ); length = ftell( f ); fseek( f, 0, SEEK_SET );
	data = malloc( (size_t)length + 1 );
	assert( fread( data, 1, (size_t)length, f ) == (size_t)length );
	fclose( f );
	*size = (size_t)length;
	return data;
}

int main( void )
{
	unsigned char *real, *before, synth[4096];
	size_t size, length;
	int i;

	/* the table: each pair the same length, one character apart */
	for( i = 0; i < COF_BONUS_LINES; i++ )
	{
		const char *a = cof_bonus_lines[i].locked, *b = cof_bonus_lines[i].unlocked;
		size_t n = strlen( a ), j, diff = 0;
		assert( n == strlen( b ) && n == 20 );
		for( j = 0; j < n; j++ ) diff += a[j] != b[j];
		assert( diff == 1 );
	}

	/* a game folder without the file: unlocked asks for one, every gated line unlocked */
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 1 );
	assert( length > 0 && length < sizeof( synth ) && All( synth, length, 1 ));
	assert( Read( synth, length, COF_BONUS_FILE_LINES, "x", "y" ) == 0 && Read( synth, length, 47, "a", "b" ) == 0 );
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 0 );
	assert( All( synth, length, 0 ));
	assert( CoF_BonusSynthesize( synth, 16, 1 ) == 0 ); /* too small: nothing */

	/* the names it answers for */
	assert( CoF_BonusIsSettings( "scriptsettings.dat" ) && CoF_BonusIsSettings( "SCRIPTSETTINGS.DAT" ));
	assert( !CoF_BonusIsSettings( "scriptsettings.dat.bak" ) && !CoF_BonusIsSettings( "alt.dat" ) && !CoF_BonusIsSettings( NULL ));

	/* the user's real file, every unlock locked: unlocked, then locked again, nothing else changed */
	if(( real = Load( locked_path, &size )) != NULL )
	{
		before = malloc( size ); memcpy( before, real, size );
		assert( All( real, size, 0 ));
		assert( CoF_BonusRewrite( real, size, 1 ) == COF_BONUS_LINES && All( real, size, 1 ));
		Untouched( before, real, size );
		assert( CoF_BonusRewrite( real, size, 0 ) == COF_BONUS_LINES && !memcmp( before, real, size ));
		assert( CoF_BonusRewrite( real, size, 0 ) == 0 ); /* already so */
		free( before ); free( real );
		puts( "locked file checked" );
	}
	/* a copy with nearly everything unlocked: locked for this session, the rest kept */
	if(( real = Load( unlocked_path, &size )) != NULL )
	{
		before = malloc( size ); memcpy( before, real, size );
		assert( All( real, size, 1 ));
		assert( CoF_BonusRewrite( real, size, 0 ) == COF_BONUS_LINES && All( real, size, 0 ));
		Untouched( before, real, size );
		free( before ); free( real );
		puts( "unlocked file checked" );
	}
	puts( "bonus room scenarios passed" );
	return 0;
}
