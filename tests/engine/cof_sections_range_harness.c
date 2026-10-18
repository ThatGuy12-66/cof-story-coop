/* cof_sections.h: the view distance a member map's own worldspawn gives. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sections_under_test.h"

static void range( const char *what, const char *ents )
{
	printf( "%s: %g\n", what, CoF_SectMaxRange( ents ));
}

int main( void )
{
	range( "MaxRange", "{\n\"classname\" \"worldspawn\"\n\"MaxRange\" \"1500\"\n}\n{\n\"classname\" \"light\"\n\"MaxRange\" \"9\"\n}\n" );
	range( "none", "{\n\"classname\" \"worldspawn\"\n}\n" );
	range( "zero", "{\n\"MaxRange\" \"0\"\n\"classname\" \"worldspawn\"\n}\n" );
	range( "not a number", "{\n\"classname\" \"worldspawn\"\n\"MaxRange\" \"far\"\n}\n" );
	range( "only in a later entity", "{\n\"classname\" \"worldspawn\"\n}\n{\n\"MaxRange\" \"700\"\n}\n" );
	range( "no entities", "" );
	return 0;
}
