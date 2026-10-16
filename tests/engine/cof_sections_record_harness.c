/* cof_sections.h: the session record of merged maps (merged.txt). */
#include "sections_under_test.h"

static void read_one( const char *label, const char *text )
{
	int merged = 0, ok;
	char section[COF_SECT_NAME];
	ok = CoF_SectionsRecordParse( text, text ? strlen( text ) : 0, &merged, section, sizeof( section ));
	printf( "read %s: %d %d %s\n", label, ok, merged, ok ? section : "-" );
}

int main( void )
{
	char out[128], *p;
	char longer[200];
	size_t n = CoF_SectionsRecord( out, sizeof( out ), 1, "c_apartment4" );
	for( p = out; *p; p++ ) if( *p == '\n' ) *p = '|';
	printf( "write: %s\n", n ? out : "(none)" );
	read_one( "merged on", "merged 1\nsection c_apartment4\n" );
	read_one( "merged off", "merged 0\nsection c_apartment1\n" );
	read_one( "none", NULL );
	read_one( "junk", "merged maybe\nsection x\n" );
	memset( longer, 'a', sizeof( longer ));
	memcpy( longer, "merged 1\nsection ", 17 );
	longer[sizeof( longer ) - 1] = 0;
	read_one( "long", longer );
	return 0;
}
