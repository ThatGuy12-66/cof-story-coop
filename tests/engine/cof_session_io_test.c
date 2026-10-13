#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_session_data.h"
typedef unsigned char byte;
typedef int qboolean;
typedef long fs_offset_t;
#define true 1
#define false 0
#define MAX_OSPATH 256
#define Q_snprintf snprintf
#define Q_stricmp _stricmp
#define Mem_Calloc(pool,size) calloc(1,size)
#define Mem_Realloc(pool,memory,size) realloc(memory,size)
#define Mem_Free free
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef struct { char path[256]; byte data[256]; size_t size; } file_t;
static file_t storage[40];
static int writes, renames, fail_write, fail_rename;
static void Con_Printf( const char *format, ... ) {}
static file_t *lookup( const char *path )
{
	int index;
	for( index = 0; index < 40; index++ ) if( !strcmp( storage[index].path, path )) return &storage[index];
	return NULL;
}
static file_t *FS_Open( const char *path, const char *mode, int directory )
{
	file_t *file = lookup( path );
	int index;
	if( mode[0] == 'r' ) return file;
	if( !file )
		for( index = 0; index < 40; index++ )
			if( !storage[index].path[0] ) { file = &storage[index]; strcpy( file->path, path ); break; }
	assert( file ); file->size = 0;
	return file;
}
static fs_offset_t FS_FileLength( file_t *file ) { return (long)file->size; }
static fs_offset_t FS_Read( file_t *file, void *data, size_t size )
{
	if( size > file->size ) size = file->size;
	memcpy( data, file->data, size ); return (long)size;
}
static fs_offset_t FS_Write( file_t *file, const void *data, size_t size )
{
	writes++; if( writes == fail_write ) return 0;
	assert( size <= sizeof( file->data )); memcpy( file->data, data, size ); file->size = size;
	return (long)size;
}
static int FS_Close( file_t *file ) { return 0; }
static int FS_FileExists( const char *path, int directory ) { return lookup( path ) != NULL; }
static int FS_Delete( const char *path )
{
	file_t *file = lookup( path ); if( !file ) return 0;
	memset( file, 0, sizeof( *file )); return 1;
}
static int FS_Rename( const char *source, const char *target )
{
	file_t *file = lookup( source );
	renames++;
	if( renames == fail_rename || !file || lookup( target )) return 0;
	strcpy( file->path, target ); return 1;
}
#include "session_io_under_test.c"

int main( void )
{
	cof_archive_header_t header = {COF_ARCHIVE_MAGIC, 1, 0, sizeof( header ), 0};
	cof_session_data_t *metadata = calloc( 1, sizeof( *metadata ));
	byte *archive = malloc( sizeof( header ));
	size_t size = sizeof( header );
	int kind, trial;
	char name[64], path[128];
	strcpy( metadata->map, "c_start" ); memcpy( archive, &header, size );
	for( kind = 1; kind <= 3; kind++ )
	{
		snprintf( name, sizeof( name ), "c_start.HL%d", kind );
		assert( CoF_SessionAppend( &archive, &size, name, "new", 3 ));
	}
	for( trial = 0; trial < 3; trial++ )
	{
		memset( storage, 0, sizeof( storage )); fail_write = fail_rename = 0;
		for( kind = 1; kind <= 3; kind++ )
		{
			snprintf( path, sizeof( path ), "save/c_start.HL%d", kind );
			assert( CoF_SessionWriteFile( path, "old", 3 ));
		}
		writes = renames = 0;
		if( trial == 0 ) fail_write = 2;
		if( trial == 1 ) fail_rename = 4;
		assert( CoF_SessionStage( archive, metadata ) == ( trial == 2 ));
		for( kind = 1; kind <= 3; kind++ )
		{
			file_t *file;
			snprintf( path, sizeof( path ), "save/c_start.HL%d", kind ); file = lookup( path );
			assert( file && file->size == 3 && !memcmp( file->data, trial == 2 ? "new" : "old", 3 ));
		}
	}
	for( trial = 0; trial < 3; trial++ )
	{
		memset( storage, 0, sizeof( storage )); fail_write = fail_rename = 0;
		assert( CoF_SessionWriteFile( "save/cofcoop/manual.cof", "old", 3 ));
		writes = renames = 0;
		if( trial == 0 ) fail_write = 1;
		if( trial == 1 ) fail_rename = 2;
		assert( CoF_SessionReplace( "save/cofcoop/manual.cof", (const byte *)"new", 3 ) == ( trial == 2 ));
		assert( !memcmp( lookup( "save/cofcoop/manual.cof" )->data, trial == 2 ? "new" : "old", 3 ));
	}
	free( archive ); free( metadata );
	puts( "session staging scenarios passed" );
	return 0;
}
