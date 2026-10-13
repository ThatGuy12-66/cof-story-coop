/* The inventory descriptor reader, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_descriptor.h"

int main( void )
{
	/* A real descriptor, CRLF as shipped (inventoryitems/apartmentkey.txt). */
	static const char key[] =
		"\"map_name\"\r\n{\r\n\tdata\t\"apartmentkey\"\r\n}\r\n\r\n\r\n"
		"\"nice_name\"\r\n{\r\n\tdata\t\"Apartment key\"\r\n}\r\n\r\n\r\n"
		"\"classname\"\r\n{\r\n\tdata\t\"item_key\"\r\n}\r\n";
	static const char tricky[] = "\"a\"\n{\n data \"classname\"\n}\n\"classname\"\n{\n data \"item_key\"\n}\n";
	char out[64], tiny[4];

	assert( CoF_DescriptorValue( key, "nice_name", out, sizeof( out )) && !strcmp( out, "Apartment key" ));
	assert( CoF_DescriptorValue( key, "classname", out, sizeof( out )) && !strcmp( out, "item_key" ));
	assert( !CoF_DescriptorValue( key, "droppable", out, sizeof( out )) && !out[0] );
	/* a key name that also appears as a value is matched only as a key */
	assert( CoF_DescriptorValue( tricky, "classname", out, sizeof( out )) && !strcmp( out, "item_key" ));
	/* truncation keeps a terminator */
	assert( CoF_DescriptorValue( key, "nice_name", tiny, sizeof( tiny )) && !strcmp( tiny, "Apa" ));
	/* an unterminated value is no value */
	assert( !CoF_DescriptorValue( "\"k\" { data \"open", "k", out, sizeof( out )) && !out[0] );

	/* story items: directly under inventoryitems/, case kept as shipped */
	assert( CoF_IsStoryItem( "inventoryitems/apartmentkey.txt" ));
	assert( CoF_IsStoryItem( "inventoryitems/Videotape.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/weapons/weapon_mobile.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/ammo/glock.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/.txt" ));
	assert( !CoF_IsStoryItem( "" ) && !CoF_IsStoryItem( "weapon_glock" ) && !CoF_IsStoryItem( NULL ));
	/* names that could not be delivered or removed safely are not story items */
	assert( !CoF_IsStoryItem( "inventoryitems/bad;name.txt" ) && !CoF_IsStoryItem( "inventoryitems/a\"b.txt" ));
	assert( !CoF_IsStoryItem( "inventoryitems/..txt" ) && !CoF_IsStoryItem( "inventoryitems/sp ace.txt" ));
	assert( CoF_DescriptorSafe( "inventoryitems/Videotape.txt" ) && !CoF_DescriptorSafe( "a..b" ) && !CoF_DescriptorSafe( "" ));
	puts( "descriptor scenarios passed" );
	return 0;
}
