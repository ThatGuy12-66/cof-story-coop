/* 1.3.3: SV_CoF_StoryOfficialMap (sv_cof_story.c): Cry of Fear's own co-op maps keep the story layer off. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
typedef int qboolean;
static int Q_strnicmp( const char *a, const char *b, size_t n )
{
	size_t i;
	for( i = 0; i < n; i++ )
	{
		int x = tolower( (unsigned char)a[i] ), y = tolower( (unsigned char)b[i] );
		if( x != y || !x ) return x - y;
	}
	return 0;
}
#include "official_under_test.c"

int main( void )
{
	static const char *official[] = { "cof_campaign_01", "cof_campaign_01_p2", "cof_manhunt_campaign", "cof_suicide4",
		"c_cof_village2", "c_cof_coopend", "C_COF_City", "COF_Campaign_01" };
	static const char *story[] = { "c_intro", "c_start", "cm_apartment3", "c_asylum1day", "c_coffin", "cofcamp", "c_cofx",
		"c_game_menu1", "", "c_co" };
	size_t i;
	for( i = 0; i < sizeof( official ) / sizeof( official[0] ); i++ )
		if( !SV_CoF_StoryOfficialMap( official[i] )) { printf( "not official: %s\n", official[i] ); return 1; }
	for( i = 0; i < sizeof( story ) / sizeof( story[0] ); i++ )
		if( SV_CoF_StoryOfficialMap( story[i] )) { printf( "official: %s\n", story[i] ); return 1; }
	if( SV_CoF_StoryOfficialMap( NULL )) return 1;
	puts( "official map scenarios passed" );
	return 0;
}
