/* 1.1.8: the chapter-start kits (sv_cof_chapter_kit.h), printed for the test to compare with the app's chapters. */
#include <stdio.h>
#include <string.h>
#include "kit_under_test.h"

int main(int argc, char **argv)
{
	static const char *maps[] = { "c_intro", "c_nightmare", "c_start", "c_basement", "c_sewer3", "c_park3",
		"c_subwaysick2", "c_subway2st1", "c_bridge", "c_attic", "c_ending1b", "c_ending2b", "c_ending3b", "c_ending1",
		"c_ending2", "c_ending3", "c_ending4", "c_ending5", "C_ATTIC", "", NULL };
	size_t i;
	int w;
	(void)argc; (void)argv;
	for( i = 0; i < sizeof( maps ) / sizeof( maps[0] ); i++ )
	{
		const cof_chapter_kit_t *kit = CoF_ChapterKit( maps[i] );
		printf( "%s:", maps[i] ? maps[i] : "(null)" );
		if( !kit ) { printf( " none\n" ); continue; }
		for( w = 0; w < COF_CHAPTER_WEAPONS && kit->weapons[w]; w++ ) printf( " %s", kit->weapons[w] );
		printf( " doctor=%d three=%d", kit->doctor_weapon, kit->three_slots );
		if( kit->global ) printf( " global=%s", kit->global ); /* 1.1.9 (C1) */
		printf( "\n" );
	}
	puts( "kits printed" );
	return 0;
}
