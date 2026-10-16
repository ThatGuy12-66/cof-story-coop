/* cof_sections.h: parse a real .sections file (written by tools/map_merge/rules.py) and print what the engine
   reads, for the test to compare with the Python side. */
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

static cof_sections_t s;

int main( void )
{
	char *text = read_file( "sections.txt" );
	int i;
	if( !text || !CoF_SectionsParse( text, &s )) { printf( "parse failed\n" ); return 1; }
	printf( "name %s count %d\n", s.name, s.count );
	for( i = 0; i < s.count; i++ )
	{
		const cof_section_t *x = &s.sec[i];
		printf( "section %d %s dz %g first %d models %d zmin %g zmax %g renames %d styles %d landmarks %d\n", i, x->map,
			x->dz, x->first_model, x->models, x->zmin, x->zmax, x->num_renames, x->num_styles, x->num_landmarks );
	}
	printf( "find c_apartment4 %d\n", CoF_SectionsFind( &s, "c_apartment4" ));
	printf( "find C_APARTMENT5 %d\n", CoF_SectionsFind( &s, "C_APARTMENT5" ));
	printf( "find c_apartment1 %d\n", CoF_SectionsFind( &s, "c_apartment1" ));
	printf( "rename 1 land1 %s\n", CoF_SectionsRename( &s.sec[1], "land1" ));
	printf( "rename 1 nothere %s\n", CoF_SectionsRename( &s.sec[1], "nothere" ));
	printf( "rename 0 land1 %s\n", CoF_SectionsRename( &s.sec[0], "land1" ));
	printf( "style 1 32 %d\n", CoF_SectionsStyle( &s.sec[1], 32 ));
	printf( "style 1 5 %d\n", CoF_SectionsStyle( &s.sec[1], 5 ));
	printf( "at 0 %d\n", CoF_SectionsAt( &s, 0.0 ));
	printf( "at %g %d\n", s.sec[1].zmin + 10, CoF_SectionsAt( &s, s.sec[1].zmin + 10 ));
	printf( "at %g %d\n", s.sec[2].zmax - 10, CoF_SectionsAt( &s, s.sec[2].zmax - 10 ));
	printf( "landmark 1 land1_m1 %d\n", CoF_SectionsHasLandmark( &s.sec[1], "land1_m1" ));
	printf( "landmark 1 land1 %d\n", CoF_SectionsHasLandmark( &s.sec[1], "land1" ));
	free( text );
	return 0;
}
