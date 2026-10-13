#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_archive.h"

int main( void )
{
	unsigned char archive[512] = {0};
	cof_archive_header_t header = {COF_ARCHIVE_MAGIC, 1, 1, 0, 0};
	cof_archive_entry_t entry = {"c_start.HL1", 3};
	size_t size, found;
	header.size = sizeof( header ) + sizeof( entry ) + 3;
	size = header.size;
	memcpy( archive, &header, sizeof( header ));
	memcpy( archive + sizeof( header ), &entry, sizeof( entry ));
	memcpy( archive + sizeof( header ) + sizeof( entry ), "abc", 3 );
	header.checksum = CoF_ArchiveChecksum( archive, size ); memcpy( archive, &header, sizeof( header ));
	assert( CoF_ArchiveValid( archive, size ));
	assert( !memcmp( CoF_ArchiveFind( archive, "c_start.HL1", &found ), "abc", 3 ) && found == 3 );
	assert( !CoF_ArchiveValid( archive, size - 1 ));
	archive[size - 1] ^= 1; assert( !CoF_ArchiveValid( archive, size )); archive[size - 1] ^= 1;
	strcpy( entry.name, "../config.cfg" ); memcpy( archive + sizeof( header ), &entry, sizeof( entry ));
	header.checksum = CoF_ArchiveChecksum( archive, size ); memcpy( archive, &header, sizeof( header ));
	assert( !CoF_ArchiveValid( archive, size ));
	strcpy( entry.name, "c_start.HL1" ); entry.size = 0xffffffffU;
	memcpy( archive + sizeof( header ), &entry, sizeof( entry ));
	header.checksum = CoF_ArchiveChecksum( archive, size ); memcpy( archive, &header, sizeof( header ));
	assert( !CoF_ArchiveValid( archive, size ));
	assert( !CoF_ArchiveName( "C:\\save" ) && !CoF_ArchiveName( "/tmp/save" ));
	assert( !CoF_ArchiveName( "NUL.cof" ) && !CoF_ArchiveName( "com1" ));
	puts( "archive scenarios passed" );
	return 0;
}
