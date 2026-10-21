/* cof_levelname.h: the level name client.dll gets in a merged map, in and out of its studio drawing, and what the
   retail prop distance rule (decomp cl_dll/gl_studiomodel.cpp, GL_DrawPoints) makes of it. */
#include <stdio.h>
#include <string.h>
#include "sections_under_test.h"
#include "levelname_under_test.h"

static cof_sections_t forest6, college2, bridge;

/* gl_studiomodel.cpp: renderfx 92 props are culled at 800 units only in these levels */
static int culled_at_800( const char *level )
{
	return strstr( level, "college1dark" ) || strstr( level, "forest1" ) || strstr( level, "forest2" ) ||
		strstr( level, "forest3" ) || strstr( level, "forest4" ) || strstr( level, "forest5" ) || strstr( level, "forest6" );
}

static void show( const char *label, const char *map, const cof_sections_t *sections, int drawing, double z )
{
	char level[80];
	snprintf( level, sizeof( level ), "maps/%s.bsp", CoF_LevelName( map, sections, drawing, z ));
	printf( "%s: %s%s\n", label, level, culled_at_800( level ) ? " (props culled at 800)" : "" );
}

int main( void )
{
	if( !CoF_SectionsParse( "cof-sections 1 cm_forest6\n"
			"section c_forest6 0 1 10\nbounds -4096 1500\n"
			"section c_forest7 4096 11 20\nbounds 2048 8000\n", &forest6 )
		|| !CoF_SectionsParse( "cof-sections 1 cm_college2\n"
			"section c_college2 0 1 10\nbounds -4096 1000\n"
			"section c_college1 3072 11 20\nbounds 1024 5000\n"
			"section c_college1dark 7168 21 30\nbounds 5120 9000\n", &college2 )
		|| !CoF_SectionsParse( "cof-sections 1 cm_bridge\n"
			"section c_bridge 0 1 10\nbounds -4096 1000\n"
			"section c_forest1 3072 11 20\nbounds 1024 5000\n", &bridge ))
	{
		printf( "parse failed\n" );
		return 1;
	}
	show( "cm_forest6, not drawing (lightmaps, HUD)", "cm_forest6", &forest6, 0, 3000.0 );
	show( "cm_forest6, props in c_forest6", "cm_forest6", &forest6, 1, 100.0 );
	show( "cm_forest6, props in c_forest7", "cm_forest6", &forest6, 1, 3000.0 );
	show( "cm_forest6, props between, nearer c_forest6", "cm_forest6", &forest6, 1, 1700.0 );
	show( "cm_college2, props in c_college1dark", "cm_college2", &college2, 1, 6000.0 );
	show( "cm_college2, props in c_college1", "cm_college2", &college2, 1, 2000.0 );
	show( "cm_college2, not drawing", "cm_college2", &college2, 0, 6000.0 );
	show( "cm_bridge, props in c_forest1", "cm_bridge", &bridge, 1, 2000.0 );
	show( "cm_bridge, props in c_bridge", "cm_bridge", &bridge, 1, 0.0 );
	show( "c_forest7 (not merged), props", "c_forest7", NULL, 1, 3000.0 );
	return 0;
}
