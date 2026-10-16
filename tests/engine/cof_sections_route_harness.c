/* cof_sections.h CoF_SectionsRoute: where a change-level request goes. */
#include "sections_under_test.h"

static char *read_file( const char *path )
{
	FILE *f = fopen( path, "rb" );
	long size;
	char *buf;
	if( !f ) return NULL;
	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );
	buf = (char *)malloc( (size_t)size + 1 );
	if( !buf || fread( buf, 1, (size_t)size, f ) != (size_t)size ) { fclose( f ); return NULL; }
	buf[size] = 0;
	fclose( f );
	return buf;
}

static cof_sections_t cur, other;

static void show( const char *label, const cof_sections_t *loaded, const char *level, const char *landmark, const cof_sections_t *dest )
{
	cof_sect_route_t r;
	CoF_SectionsRoute( loaded, level, landmark, dest, &r );
	printf( "%s -> %s %d %s dest %s source %s\n", label, r.section_move ? "move" : "load", r.to_section, r.file,
		r.dest_landmark, r.source_landmark );
}

int main( void )
{
	if( !CoF_SectionsParse( read_file( "cur.txt" ), &cur ) || !CoF_SectionsParse( read_file( "other.txt" ), &other ))
	{
		printf( "parse failed\n" );
		return 1;
	}
	show( "c_apartment4 land1", &cur, "c_apartment4", "land1", NULL );
	show( "c_apartment3 land1_m1", &cur, "c_apartment3", "land1_m1", NULL );
	show( "c_apartment5 ap5la", &cur, "c_apartment5", "ap5la", NULL );
	show( "c_apartment3 ap5la_m2", &cur, "c_apartment3", "ap5la_m2", NULL );
	show( "c_apartment1 land5", &cur, "c_apartment1", "land5", &other );
	show( "c_apartment1 land5 off", &cur, "c_apartment1", "land5", NULL );
	show( "c_city land9", &cur, "c_city", "land9", NULL );
	show( "c_apartment4 land4", &cur, "c_apartment4", "land4", NULL );
	show( "unmerged c_apartment2 land1", NULL, "c_apartment2", "land1", &other );
	return 0;
}
