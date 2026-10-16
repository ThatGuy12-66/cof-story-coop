/* cof_sections.h: malformed .sections files are refused (printed "refused" each). */
#include "sections_under_test.h"
#include "cases.h"

static cof_sections_t s;

int main( void )
{
	size_t i;
	for( i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ )
		printf( "%s\n", CoF_SectionsParse( cases[i], &s ) ? "accepted" : "refused" );
	return 0;
}
