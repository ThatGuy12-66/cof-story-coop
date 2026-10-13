#ifndef SV_COF_ARCHIVE_H
#define SV_COF_ARCHIVE_H
#include "sv_cof_snapshot.h"

#define COF_ARCHIVE_MAGIC 0x53434643U
#define COF_ARCHIVE_LIMIT (256U * 1024U * 1024U)
#define COF_ARCHIVE_FILES 1024U

typedef struct { uint32_t magic, version, count, size, checksum; } cof_archive_header_t;
typedef struct { char name[64]; uint32_t size; } cof_archive_entry_t;

static int CoF_ArchiveName( const char *name )
{
	size_t index, length;
	char stem[8] = {0};
	if( !name ) return 0;
	for( length = 0; length < 64 && name[length]; length++ ) {}
	if( length == 64 ) return 0;
	if( !length || name[0] == '.' || strstr( name, ".." )) return 0;
	for( index = 0; index < sizeof( stem ) - 1 && index < length && name[index] != '.'; index++ )
		stem[index] = name[index] >= 'a' && name[index] <= 'z' ? name[index] - 'a' + 'A' : name[index];
	if( !strcmp( stem, "CON" ) || !strcmp( stem, "PRN" ) || !strcmp( stem, "AUX" ) || !strcmp( stem, "NUL" ) ||
		( strlen( stem ) == 4 && ( !strncmp( stem, "COM", 3 ) || !strncmp( stem, "LPT", 3 )) && stem[3] >= '0' && stem[3] <= '9' )) return 0;
	for( index = 0; index < length; index++ )
		if( !( ( name[index] >= 'a' && name[index] <= 'z' ) || ( name[index] >= 'A' && name[index] <= 'Z' ) ||
			( name[index] >= '0' && name[index] <= '9' ) || name[index] == '_' || name[index] == '.' || name[index] == '-' )) return 0;
	return 1;
}

static int CoF_ArchiveSameName( const char *first, const char *second )
{
	while( *first && *second )
	{
		int left = *first++, right = *second++;
		if( left >= 'A' && left <= 'Z' ) left += 'a' - 'A';
		if( right >= 'A' && right <= 'Z' ) right += 'a' - 'A';
		if( left != right ) return 0;
	}
	return *first == *second;
}

static uint32_t CoF_ArchiveChecksum( const unsigned char *data, size_t size )
{
	return CoF_SnapshotChecksum( data, 16 ) ^ CoF_SnapshotChecksum( data + sizeof( cof_archive_header_t ), size - sizeof( cof_archive_header_t ));
}

static int CoF_ArchiveValid( const void *data, size_t size )
{
	cof_archive_header_t header;
	const unsigned char *bytes = data;
	size_t position = sizeof( header );
	uint32_t index, previous;
	const char *names[COF_ARCHIVE_FILES];
	if( !data || size < sizeof( header ) || size > COF_ARCHIVE_LIMIT ) return 0;
	memcpy( &header, data, sizeof( header ));
	if( header.magic != COF_ARCHIVE_MAGIC || header.version != 1 || header.size != size ||
		!header.count || header.count > COF_ARCHIVE_FILES || header.checksum != CoF_ArchiveChecksum( data, size )) return 0;
	for( index = 0; index < header.count; index++ )
	{
		cof_archive_entry_t entry;
		if( sizeof( entry ) > size - position ) return 0;
		memcpy( &entry, bytes + position, sizeof( entry ));
		if( !CoF_ArchiveName( entry.name )) return 0;
		for( previous = 0; previous < index; previous++ ) if( CoF_ArchiveSameName( names[previous], entry.name )) return 0;
		names[index] = (const char *)bytes + position;
		position += sizeof( entry );
		if( entry.size > size - position ) return 0;
		position += entry.size;
	}
	return position == size;
}

static const unsigned char *CoF_ArchiveFind( const void *data, const char *name, size_t *size )
{
	cof_archive_header_t header;
	const unsigned char *cursor = data;
	uint32_t index;
	memcpy( &header, data, sizeof( header )); cursor += sizeof( header );
	for( index = 0; index < header.count; index++ )
	{
		cof_archive_entry_t entry;
		memcpy( &entry, cursor, sizeof( entry )); cursor += sizeof( entry );
		if( !strcmp( entry.name, name )) { *size = entry.size; return cursor; }
		cursor += entry.size;
	}
	*size = 0;
	return NULL;
}
#endif
