/* Offline doubles for the arrival refusal's log decision (sv_cof_arrival.c, 1.1.9 X4). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define MAX_QPATH 64
#define Q_strcmp strcmp
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
static struct { int spawncount; } svs;
typedef struct
{
	int userid;
	int refused_generation;
	char refused_level[MAX_QPATH], refused_landmark[MAX_QPATH];
} cof_arrival_player_t;

#include "refusal_under_test.c"

int main( void )
{
	cof_arrival_player_t a = { 1, -1, "", "" }, b = { 2, -1, "", "" };
	int frame, lines = 0;
	svs.spawncount = 7;
	/* an arrival idles in the exit back for ten minutes at 60 frames a second: one line */
	for( frame = 0; frame < 36000; frame++ ) if( CoF_ArrivalRefusalNew( &a, "c_park2", "swheh1kl" )) lines++;
	assert( lines == 1 );
	/* another player in it: their own line; another exit: its own line */
	assert( CoF_ArrivalRefusalNew( &b, "c_park2", "swheh1kl" ) && !CoF_ArrivalRefusalNew( &b, "c_park2", "swheh1kl" ));
	assert( CoF_ArrivalRefusalNew( &a, "c_stad", "swheh1kl" ) && CoF_ArrivalRefusalNew( &a, "c_stad", "other" ));
	/* the next map: said again */
	svs.spawncount++;
	assert( CoF_ArrivalRefusalNew( &a, "c_stad", "other" ) && !CoF_ArrivalRefusalNew( &a, "c_stad", "other" ));
	puts( "arrival refusal scenarios passed" );
	return 0;
}
