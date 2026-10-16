/* cof_sections.h CoF_SectionMovers: who a section move takes. */
#include "sections_under_test.h"

static void show( const char *label, cof_sect_player_t *p, int count )
{
	int movers[8], i;
	CoF_SectionMovers( p, count, movers, 128.0f );
	printf( "%s:", label );
	for( i = 0; i < count; i++ ) printf( " %d", movers[i] );
	printf( "\n" );
}

int main( void )
{
	// alive, human, in_exit, moved_recently, origin
	cof_sect_player_t p[6] = {
		{ 1, 1, 1, 0, { 0, 0, 0 } },
		{ 1, 1, 0, 0, { 100, 0, 0 } },
		{ 1, 1, 0, 0, { 900, 0, 0 } },
		{ 1, 0, 0, 0, { 10, 0, 0 } },
		{ 0, 1, 0, 0, { 20, 0, 0 } },
		{ 1, 1, 0, 1, { 30, 0, 0 } },
	};
	show( "walk-in", p, 6 );
	p[0].in_exit = 0;
	show( "scripted", p, 6 );
	p[0].in_exit = 1;
	p[1].origin[0] = 200;
	p[2].in_exit = 1;
	p[2].origin[0] = 400;
	show( "two exits", p, 6 );
	p[0].in_exit = p[2].in_exit = 0;
	p[1].origin[0] = 100;
	p[3].in_exit = 1;
	show( "only a bot in the exit", p, 6 );
	return 0;
}
