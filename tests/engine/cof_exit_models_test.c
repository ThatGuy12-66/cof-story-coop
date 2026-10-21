/* SV_CoF_MapExitModels cut out of sv_game.c, against a small entity-script
 * parser: which brush models are this exit, and does it fire anything. */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define MAX_TOKEN 2048
#define Q_strcmp strcmp
#define Q_strlen strlen
#define Q_atoi atoi
#define Q_atof atof
static int Q_stricmp( const char *a, const char *b )
{
	for( ; *a && tolower((unsigned char)*a ) == tolower((unsigned char)*b ); a++, b++ );
	return tolower((unsigned char)*a ) - tolower((unsigned char)*b );
}
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( n >= size ) n = size - 1;
	memcpy( dst, src, n ); dst[n] = 0;
	return n;
}
static const char *map_text =
	"{\n\"classname\" \"worldspawn\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"C_Start2\"\n\"landmark\" \"hahaland\"\n\"model\" \"*3\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_apartment1\"\n\"landmark\" \"land1\"\n\"model\" \"*4\"\n\"target\" \"relay\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_roof\"\n\"landmark\" \"land2\"\n\"model\" \"*5\"\n\"changetarget\" \"x\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_sewer\"\n\"landmark\" \"land3\"\n\"model\" \"*7\"\n\"killtarget\" \"y\"\n}\n"
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"z\"\n\"model\" \"*6\"\n}\n"
	/* use-only exits fired by a trigger_multiple; the first trigger comes before its exit */
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"cl-hall1\"\n\"killtarget\" \"lamp\"\n\"wait\" \"1\"\n\"model\" \"*9\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall1\"\n\"landmark\" \"land4\"\n\"model\" \"*8\"\n\"targetname\" \"cl-hall1\"\n\"spawnflags\" \"2\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall2\"\n\"landmark\" \"land5\"\n\"model\" \"*10\"\n\"targetname\" \"cl-hall2\"\n\"spawnflags\" \"2\"\n}\n"
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"cl-hall2\"\n\"wait\" \"1\"\n\"model\" \"*11\"\n}\n"
	"{\n\"classname\" \"func_door\"\n\"targetname\" \"cl-hall2\"\n\"model\" \"*12\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall3\"\n\"landmark\" \"land6\"\n\"model\" \"*13\"\n\"targetname\" \"cl-hall3\"\n\"spawnflags\" \"2\"\n}\n"
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"cl-hall3\"\n\"noise\" \"doors/creak.wav\"\n\"message\" \"Locked\"\n\"wait\" \"1\"\n\"model\" \"*14\"\n}\n"
	/* a func_door sharing a name nothing targets does not mark the exit named the same */
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall4\"\n\"landmark\" \"land7\"\n\"model\" \"*15\"\n\"targetname\" \"cl-hall4\"\n}\n"
	"{\n\"classname\" \"func_door\"\n\"targetname\" \"cl-hall4\"\n\"model\" \"*16\"\n}\n"
	/* fired by a trigger_once (it removes itself), and by a trigger_multiple that waits -1 (the same) */
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall5\"\n\"landmark\" \"land8\"\n\"model\" \"*17\"\n\"targetname\" \"cl-hall5\"\n}\n"
	"{\n\"classname\" \"trigger_once\"\n\"target\" \"cl-hall5\"\n\"model\" \"*18\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_hall6\"\n\"landmark\" \"land9\"\n\"model\" \"*19\"\n\"targetname\" \"cl-hall6\"\n}\n"
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"cl-hall6\"\n\"wait\" \"-1\"\n\"model\" \"*20\"\n}\n";
static char *SV_ReadEntityScript( const char *map, int *flags ) { *flags = 0; return (char *)map_text; }
static void Mem_Free( void *p ) { assert( p == map_text ); }
static char *COM_ParseFile( char *data, char *token, int size )
{
	int n = 0;
	if( !data ) return NULL;
	while( *data && isspace((unsigned char)*data )) data++;
	if( !*data ) return NULL;
	if( *data == '"' )
	{
		for( data++; *data && *data != '"'; data++ )
			if( n < size - 1 ) token[n++] = *data;
		if( *data ) data++;
	}
	else token[n++] = *data++; /* a brace */
	token[n] = 0;
	return data;
}

#include "models_under_test.c"

static int real_models( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires, qboolean *scripted )
{
	return SV_CoF_MapExitModels( map, destination, landmark, models, max, fires, scripted );
}
static int read_models( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires )
{
	return SV_CoF_MapExitModels( map, destination, landmark, models, max, fires, NULL );
}
#define SV_CoF_MapExitModels read_models

int main( void )
{
	int models[4];
	qboolean fires = true;
	/* a plain story exit: its model, and nothing fired; the map key's case is ignored */
	assert( SV_CoF_MapExitModels( "c_start", "c_start2", "hahaland", models, 4, &fires ) == 1 && models[0] == 3 && !fires );
	/* each of the three keys ChangeLevelNow re-fires marks the exit */
	assert( SV_CoF_MapExitModels( "c_start", "c_apartment1", "land1", models, 4, &fires ) == 1 && models[0] == 4 && fires );
	assert( SV_CoF_MapExitModels( "c_start", "c_roof", "land2", models, 4, &fires ) == 1 && fires );
	assert( SV_CoF_MapExitModels( "c_start", "c_sewer", "land3", models, 4, &fires ) == 1 && fires );
	/* the older caller passes no flag; another exit's or trigger's target does not count */
	assert( SV_CoF_MapExitModels( "c_start", "c_start2", "hahaland", models, 4, NULL ) == 1 );
	fires = true;
	assert( SV_CoF_MapExitModels( "c_start", "c_nowhere", "hahaland", models, 4, &fires ) == 0 && !fires );
	/* Final review F2: the trigger that fires a use-only exit fires again every
	   wait while the waiter stands in it. Its own killtarget would repeat: */
	assert( SV_CoF_MapExitModels( "c_start", "c_hall1", "land4", models, 4, &fires ) == 2 && models[0] == 8 && models[1] == 9 && fires );
	/* ...and so would toggling a func_door that shares the exit's name */
	assert( SV_CoF_MapExitModels( "c_start", "c_hall2", "land5", models, 4, &fires ) == 2 && models[0] == 10 && models[1] == 11 && fires );
	/* a firing trigger's noise or message repeating is accepted */
	assert( SV_CoF_MapExitModels( "c_start", "c_hall3", "land6", models, 4, &fires ) == 2 && models[0] == 13 && models[1] == 14 && !fires );
	/* a namesake with no trigger firing the exit repeats nothing */
	assert( SV_CoF_MapExitModels( "c_start", "c_hall4", "land7", models, 4, &fires ) == 1 && models[0] == 15 && !fires );
	/* the transition's own caller passes no flag: the firing trigger's brush is part of the exit all the same */
	assert( SV_CoF_MapExitModels( "c_start", "c_hall1", "land4", models, 4, NULL ) == 2 && models[0] == 8 && models[1] == 9 );
	{
		/* 1.1.8 (E1 F1/F2): a trigger_multiple firing an exit makes it a walk-in exit, not a scripted one; a
		   trigger_once, or a trigger_multiple that waits -1, removes itself: scripted */
		qboolean scripted = true;
		assert( real_models( "c_start", "c_hall3", "land6", models, 4, &fires, &scripted ) == 2 && !scripted );
		assert( real_models( "c_start", "c_hall5", "land8", models, 4, &fires, &scripted ) == 1 && models[0] == 17 && scripted );
		assert( real_models( "c_start", "c_hall6", "land9", models, 4, &fires, &scripted ) == 1 && models[0] == 19 && scripted );
		assert( real_models( "c_start", "c_start2", "hahaland", models, 4, &fires, &scripted ) == 1 && !scripted );
	}
	puts( "exit model scenarios passed" );
	return 0;
}
