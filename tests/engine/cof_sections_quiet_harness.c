/* Offline doubles for the section exit's model cache and its "nobody" log decision (sv_cof_sections.c, 1.1.9 X3). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define MAX_QPATH 64
#define Q_strlen strlen
#define Q_strcmp strcmp
#define Q_min( a, b ) (( a ) < ( b ) ? ( a ) : ( b ))
#define ARRAYSIZE( x ) ( sizeof( x ) / sizeof(( x )[0] ))
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
static struct { int spawncount; } svs;
static struct { double time; const char *name; } sv;
static int reads;
/* the merged map's script: two exits, 7 and 9 */
static int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires, qboolean *scripted )
{
	assert( !strcmp( map, "cm_apartment1" ) && !fires && !scripted && max == 16 );
	reads++;
	models[0] = !strcmp( destination, "c_apartment1" ) ? 7 : 9;
	models[1] = 11;
	return 2;
}

#include "quiet_under_test.c"

int main( void )
{
	int models[16], frame, lines = 0;
	sv.name = "cm_apartment1"; svs.spawncount = 3; sv.time = 100;
	/* a player idles in the exit back for 600 frames: the script is read once, one line is logged */
	for( frame = 0; frame < 600; frame++, sv.time += 0.016 )
	{
		memset( models, 0, sizeof( models ));
		assert( CoF_SectExitModels( "c_apartment1", "land1", models, 16 ) == 2 && models[0] == 7 && models[1] == 11 );
		if( CoF_SectNobodyNew( "c_apartment1", "land1" )) lines++;
	}
	assert( reads == 1 && lines == 1 );
	/* a second player idles in the other exit meanwhile: its own line, its own read, no repeats for either */
	for( frame = 0; frame < 600; frame++, sv.time += 0.016 )
	{
		assert( CoF_SectExitModels( frame & 1 ? "c_apartment2" : "c_apartment1", "land1", models, 16 ) == 2 );
		if( CoF_SectNobodyNew( frame & 1 ? "c_apartment2" : "c_apartment1", "land1" )) lines++;
	}
	assert( lines == 2 );
	/* fewer models wanted than the exit has: only that many */
	models[1] = -1; assert( CoF_SectExitModels( "c_apartment2", "land1", models, 1 ) == 1 && models[0] == 9 && models[1] == -1 );
	/* a new idle after a quiet spell is logged again */
	sv.time += 11; assert( CoF_SectNobodyNew( "c_apartment1", "land1" ) && !CoF_SectNobodyNew( "c_apartment1", "land1" ));
	/* the next map reads its own script and logs afresh */
	svs.spawncount++; reads = 0;
	assert( CoF_SectExitModels( "c_apartment1", "land1", models, 16 ) == 2 && reads == 1 );
	assert( CoF_SectNobodyNew( "c_apartment1", "land1" ));
	/* no landmark (the game's own restarts): not kept, asked as before */
	assert( CoF_SectExitModels( "c_apartment1", NULL, models, 16 ) == 2 && reads == 2 );
	assert( CoF_SectNobodyNew( "c_apartment1", NULL ) && !CoF_SectNobodyNew( "c_apartment1", NULL ));
	puts( "section quiet scenarios passed" );
	return 0;
}
