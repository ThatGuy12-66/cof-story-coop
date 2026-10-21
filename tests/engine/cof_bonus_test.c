/* The bonus room and costume switches (sv_cof_bonus.h): scriptsettings.dat as the game reads it.
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

static int All( const unsigned char *text, size_t size, const cof_bonus_line_t *table, int count, int want )
{
	int i;
	for( i = 0; i < count; i++ )
		if( Read( text, size, table[i].line, table[i].locked, table[i].unlocked ) != want ) return 0;
	return 1;
}

static int Gated( const cof_bonus_line_t *table, int count, int line )
{
	int i;
	for( i = 0; i < count; i++ ) if( table[i].line == line ) return 1;
	return 0;
}

/* every line the switch does not own is byte for byte what it was */
static void Untouched( const unsigned char *before, const unsigned char *after, size_t size, const cof_bonus_line_t *table, int count )
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
		if( !Gated( table, count, line )) assert( !memcmp( before + start, after + start, i - start ));
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

/* each pair the same length, one character apart; no line in both tables */
static void Table( const cof_bonus_line_t *table, int count )
{
	int i;
	for( i = 0; i < count; i++ )
	{
		const char *a = table[i].locked, *b = table[i].unlocked;
		size_t n = strlen( a ), j, diff = 0;
		assert( n == strlen( b ) && n == 20 );
		for( j = 0; j < n; j++ ) diff += a[j] != b[j];
		assert( diff == 1 );
		assert( table[i].line <= COF_BONUS_FILE_LINES );
	}
}

/* After 1.1.9: the engine's own reading of a bit is the game's (Read above) on every line of both tables */
static void SameReading( const unsigned char *text, size_t size )
{
	int i;
	for( i = 0; i < COF_BONUS_LINES; i++ )
		assert( CoF_BonusUnlocked( text, size, &cof_bonus_lines[i] ) == ( Read( text, size, cof_bonus_lines[i].line, cof_bonus_lines[i].locked, cof_bonus_lines[i].unlocked ) == 1 ));
	for( i = 0; i < COF_COSTUME_LINES; i++ )
		assert( CoF_BonusUnlocked( text, size, &cof_costume_lines[i] ) == ( Read( text, size, cof_costume_lines[i].line, cof_costume_lines[i].locked, cof_costume_lines[i].unlocked ) == 1 ));
}

/* the bonus kit's checks and the wardrobe key (cof_wardrobe) */
static void KitAndWardrobe( void )
{
	unsigned char synth[4096], message[COF_WARDROBE_MESSAGE];
	size_t length;
	unsigned int flags;
	int any = -1, i;
	static const unsigned char odd[] = "\n\n-\n-\nz3,4rj)4b!9g7vgi4x5a\n"; /* line 3 (ending 5) unlocked: the door opens */
	/* the door opens on any ending; a file of nothing but locked lines or no file at all keeps it shut */
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 1, 1 );
	assert( CoF_BonusDoorOpen( synth, length )); SameReading( synth, length );
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 0, 0 );
	assert( !CoF_BonusDoorOpen( synth, length )); SameReading( synth, length );
	assert( !CoF_BonusDoorOpen( NULL, 0 ) && !CoF_BonusUnlocked( NULL, 0, &cof_bonus_lines[0] ) && !CoF_BonusUnlocked( synth, length, NULL ));
	assert( CoF_BonusDoorOpen( odd, sizeof( odd ) - 1 )); SameReading( odd, sizeof( odd ) - 1 );
	/* a line holding both texts reads locked, as the game reads it; the terminator ends the file */
	{
		unsigned char both[256];
		int n = snprintf( (char *)both, sizeof( both ), "-\n-\n%s %s\n", cof_bonus_lines[4].unlocked, cof_bonus_lines[4].locked );
		assert( !CoF_BonusUnlocked( both, (size_t)n, &cof_bonus_lines[4] )); SameReading( both, (size_t)n );
		n = snprintf( (char *)both, sizeof( both ), "-\n-%c\n%s\n", 0, cof_bonus_lines[4].unlocked );
		assert( !CoF_BonusUnlocked( both, (size_t)n, &cof_bonus_lines[4] )); SameReading( both, (size_t)n );
	}
	/* one ending: the door opens, the room's own items keep their own lines */
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 0, 0 );
	CoF_BonusRewriteTable( synth, length, &cof_bonus_lines[1], 1, 1 );
	assert( CoF_BonusDoorOpen( synth, length ) && !CoF_BonusUnlocked( synth, length, CoF_BonusLine( 39 ))); SameReading( synth, length );

	/* the wardrobe's outfits: bit k for line 51 + k, canseeclothes when any is open (CCoFClothesMenu::Use) */
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 0, 1 );
	assert( CoF_WardrobeFlags( synth, length, &any ) == 0xFFF && any == 1 );
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 1, 0 );
	assert( CoF_WardrobeFlags( synth, length, &any ) == 0 && any == 0 );
	CoF_BonusRewriteTable( synth, length, &cof_costume_lines[2], 1, 1 );   /* the Hello Kitty suit */
	CoF_BonusRewriteTable( synth, length, &cof_costume_lines[11], 1, 1 );  /* the custom hoodie */
	flags = CoF_WardrobeFlags( synth, length, &any );
	assert( flags == ( 4u | 0x800u ) && any == 1 && CoF_WardrobeFlags( synth, length, NULL ) == flags );
	for( i = 0; i < COF_COSTUME_LINES; i++ )
		assert( !!( flags & ( 1u << i )) == ( Read( synth, length, cof_costume_lines[i].line, cof_costume_lines[i].locked, cof_costume_lines[i].unlocked ) == 1 ));
	/* the message: LONG (little-endian, as WRITE_LONG) the outfits, BYTE canseeclothes, BYTE the outfit worn, BYTE 1 */
	memset( message, 0xAA, sizeof( message ));
	CoF_WardrobeMessage( message, 0x804u, 1, 3 );
	assert( COF_WARDROBE_MESSAGE == 7 );
	assert( message[0] == 0x04 && message[1] == 0x08 && message[2] == 0 && message[3] == 0 && message[4] == 1 && message[5] == 3 && message[6] == 1 );
	CoF_WardrobeMessage( message, 0x12345678u, 0, 12 );
	assert( message[0] == 0x78 && message[1] == 0x56 && message[2] == 0x34 && message[3] == 0x12 && message[4] == 0 && message[5] == 12 && message[6] == 1 );
	/* cof_wardrobe's answer: open only with the room open, alive, free; a second press within the wait: nothing */
	assert( CoF_WardrobeAnswer( 1, 1, 0, 0, 0, 0, 0 ) == COF_WARDROBE_OPEN );
	assert( CoF_WardrobeAnswer( 0, 1, 0, 0, 0, 0, 0 ) == COF_WARDROBE_SHUT );
	assert( CoF_WardrobeAnswer( 1, 0, 0, 0, 0, 0, 0 ) == COF_WARDROBE_DEAD );
	assert( CoF_WardrobeAnswer( 1, 1, 1, 0, 0, 0, 0 ) == COF_WARDROBE_SCENE && CoF_WardrobeAnswer( 1, 1, 0, 1, 0, 0, 0 ) == COF_WARDROBE_SCENE );
	assert( CoF_WardrobeAnswer( 1, 1, 0, 0, 1, 0, 0 ) == COF_WARDROBE_LADDER );
	assert( CoF_WardrobeAnswer( 1, 1, 0, 0, 0, 1, 0 ) == COF_WARDROBE_BOAT );
	assert( CoF_WardrobeAnswer( 1, 1, 0, 0, 0, 0, 1 ) == COF_WARDROBE_SOON && CoF_WardrobeAnswer( 0, 0, 1, 1, 1, 1, 1 ) == COF_WARDROBE_SOON );
	assert( CoF_WardrobeAnswer( 0, 0, 1, 1, 1, 1, 0 ) == COF_WARDROBE_SHUT && CoF_WardrobeAnswer( 1, 0, 1, 1, 1, 1, 0 ) == COF_WARDROBE_DEAD );
	/* the Close of the key's menu goes no further, unless its sender uses the real wardrobe's running camera now */
	assert( CoF_WardrobeKeepsClose( 1, 0 ) && !CoF_WardrobeKeepsClose( 1, 1 ) && !CoF_WardrobeKeepsClose( 0, 0 ) && !CoF_WardrobeKeepsClose( 0, 1 ));
}

int main( void )
{
	unsigned char *real, *before, synth[4096];
	size_t size, length;
	int i;

	KitAndWardrobe();

	Table( cof_bonus_lines, COF_BONUS_LINES );
	Table( cof_costume_lines, COF_COSTUME_LINES );
	assert( COF_COSTUME_LINES == 12 );
	for( i = 0; i < COF_COSTUME_LINES; i++ )
		assert( cof_costume_lines[i].line == 51 + i && !Gated( cof_bonus_lines, COF_BONUS_LINES, cof_costume_lines[i].line ));

	/* a game folder without the file: one is made with each switch's lines as asked, every other line locked */
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 1, 1 );
	assert( length > 0 && length < sizeof( synth ) && All( synth, length, cof_bonus_lines, COF_BONUS_LINES, 1 ));
	assert( All( synth, length, cof_costume_lines, COF_COSTUME_LINES, 1 ));
	assert( Read( synth, length, COF_BONUS_FILE_LINES, "x", "y" ) == 0 && Read( synth, length, 47, "a", "b" ) == 0 );
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 0, 1 );
	assert( All( synth, length, cof_bonus_lines, COF_BONUS_LINES, 0 ) && All( synth, length, cof_costume_lines, COF_COSTUME_LINES, 1 ));
	length = CoF_BonusSynthesize( synth, sizeof( synth ), 1, -1 );
	assert( All( synth, length, cof_bonus_lines, COF_BONUS_LINES, 1 ) && All( synth, length, cof_costume_lines, COF_COSTUME_LINES, 0 ));
	assert( CoF_BonusSynthesize( synth, 16, 1, 1 ) == 0 ); /* too small: nothing */

	/* the names it answers for */
	assert( CoF_BonusIsSettings( "scriptsettings.dat" ) && CoF_BonusIsSettings( "SCRIPTSETTINGS.DAT" ));
	assert( !CoF_BonusIsSettings( "scriptsettings.dat.bak" ) && !CoF_BonusIsSettings( "alt.dat" ) && !CoF_BonusIsSettings( NULL ));

	/* the user's real file, every unlock locked: unlocked, then locked again, nothing else changed */
	if(( real = Load( locked_path, &size )) != NULL )
	{
		before = malloc( size ); memcpy( before, real, size );
		assert( All( real, size, cof_bonus_lines, COF_BONUS_LINES, 0 ) && All( real, size, cof_costume_lines, COF_COSTUME_LINES, 0 ));
		SameReading( real, size ); assert( !CoF_BonusDoorOpen( real, size ) && CoF_WardrobeFlags( real, size, NULL ) == 0 );
		assert( CoF_BonusRewrite( real, size, 1 ) == COF_BONUS_LINES && All( real, size, cof_bonus_lines, COF_BONUS_LINES, 1 ));
		SameReading( real, size ); assert( CoF_BonusDoorOpen( real, size ));
		Untouched( before, real, size, cof_bonus_lines, COF_BONUS_LINES );
		assert( CoF_BonusRewrite( real, size, 0 ) == COF_BONUS_LINES && !memcmp( before, real, size ));
		assert( CoF_BonusRewrite( real, size, 0 ) == 0 ); /* already so */
		/* the wardrobe: every outfit open, only its lines changed */
		assert( CoF_CostumeRewrite( real, size, 1 ) == COF_COSTUME_LINES && All( real, size, cof_costume_lines, COF_COSTUME_LINES, 1 ));
		Untouched( before, real, size, cof_costume_lines, COF_COSTUME_LINES );
		assert( CoF_CostumeRewrite( real, size, 0 ) == COF_COSTUME_LINES && !memcmp( before, real, size ));
		free( before ); free( real );
		puts( "locked file checked" );
	}
	/* a copy with nearly everything unlocked: locked for this session, the rest kept */
	if(( real = Load( unlocked_path, &size )) != NULL )
	{
		before = malloc( size ); memcpy( before, real, size );
		assert( All( real, size, cof_bonus_lines, COF_BONUS_LINES, 1 ));
		SameReading( real, size ); assert( CoF_BonusDoorOpen( real, size ));
		assert( CoF_BonusRewrite( real, size, 0 ) == COF_BONUS_LINES && All( real, size, cof_bonus_lines, COF_BONUS_LINES, 0 ));
		SameReading( real, size ); assert( !CoF_BonusDoorOpen( real, size ));
		Untouched( before, real, size, cof_bonus_lines, COF_BONUS_LINES );
		free( before ); free( real );
		puts( "unlocked file checked" );
	}
	puts( "bonus room scenarios passed" );
	return 0;
}
