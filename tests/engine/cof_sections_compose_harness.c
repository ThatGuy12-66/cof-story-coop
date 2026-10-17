/* cof_sections.h: compose a merged map's entities from the real member lumps; print the length and FNV-1a 64 of
   the result for the test to compare with tools/map_merge/rules.py compose_reference. */
#include "sections_under_test.h"
#include "members.h"

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
	char *members[MEMBERS], *world = read_file( "world.ent" ), *text = read_file( "sections.txt" ), *out;
	char path[32];
	size_t len, i;
	unsigned long long h = 0xcbf29ce484222325ULL;
	int k;
	for( k = 0; k < MEMBERS; k++ )
	{
		sprintf( path, "member%d.ent", k );
		members[k] = read_file( path );
		if( !members[k] ) { printf( "no %s\n", path ); return 1; }
	}
	if( !world || !text || !CoF_SectionsParse( text, &s )) { printf( "bad input\n" ); return 1; }
	out = CoF_ComposeEntities( world, (const char *const *)members, MEMBERS, &s, &len );
	if( !out ) { printf( "compose failed\n" ); return 1; }
	for( i = 0; i < len; i++ )
		h = ( h ^ (unsigned char)out[i] ) * 0x100000001b3ULL;
	printf( "%u %016llx\n", (unsigned)len, h );
#ifdef PRINT_TEXT
	fwrite( out, 1, len, stdout );
#endif
	free( out );
	return 0;
}
