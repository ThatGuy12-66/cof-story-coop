"""The story item give through the retail layer, against the retail harness's offline doubles (1.1.8)."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes

# Replaces the retail harness's descriptor files and its give (cof_retail_give_test.c renames those away).
GIVE_DOUBLES = r'''
#undef FS_LoadFile
#undef SV_CoF_StoryGiveItemEx
static char give_seen_class[64];
static int give_seen_pressed, give_refuse;
static byte *FS_LoadFile( const char *path, fs_offset_t *size, qboolean gamedironly )
{
	static const char hallkey[] = "\"nice_name\"\r\n{\r\n\tdata\t\"Hall key\"\r\n}\r\n\"classname\"\r\n{\r\n\tdata\t\"item_key\"\r\n}\r\n";
	static const char taclight[] = "\"nice_name\"\r\n{\r\n\tdata\t\"Tactical light\"\r\n}\r\n\"classname\"\r\n{\r\n\tdata\t\"item_glocktaclight\"\r\n}\r\n";
	static const char battery[] = "\"nice_name\"\r\n{\r\n\tdata\t\"Phone battery\"\r\n}\r\n\"classname\"\r\n{\r\n\tdata\t\"item_phonebattery\"\r\n}\r\n";
	const char *text = !strcmp( path, "inventoryitems/hallkey.txt" ) ? hallkey : !strcmp( path, "inventoryitems/taclight.txt" ) ? taclight :
		!strcmp( path, "inventoryitems/mobile_battery.txt" ) ? battery : NULL;
	char *copy;
	if( !text ) return NULL;
	copy = malloc( strlen( text ) + 1 );
	strcpy( copy, text );
	if( size ) *size = (fs_offset_t)strlen( text );
	return (byte *)copy;
}
/* The game's pickup: a CItem's MyTouch takes the item only with IN_USE freshly pressed; KeyitemUse looks at no button. */
static qboolean SV_CoF_StoryGiveItemEx( sv_client_t *cl, const char *classname, const char *message )
{
	give_calls++;
	Q_strncpy( give_seen_class, classname, sizeof( give_seen_class ));
	memcpy( &give_seen_pressed, (byte *)cl->edict->pvPrivateData + 0x22B0, sizeof( give_seen_pressed ));
	if( give_refuse ) return false;
	return !strcmp( classname, "item_key" ) || ( give_seen_pressed & 32 ) != 0;
}
'''


class RetailGiveTests(unittest.TestCase):
    def test_give_presses_use_for_the_item_pickups(self):
        out = run_native(self, 'cof_retail_give_test.c', {
            'retail_fixture.c': (ENGINE_SERVER.parents[4] / 'tests/engine/cof_retail_harness.c').read_text(),
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_retail.h': (ENGINE_SERVER / 'sv_cof_retail.h').read_text(),
            'sv_cof_state_data.h': (ENGINE_SERVER / 'sv_cof_state_data.h').read_text(),
            'sv_cof_light.h': (ENGINE_SERVER / 'sv_cof_light.h').read_text(),
            'cof_strip.h': (ENGINE_SERVER.parent / 'common/cof_strip.h').read_text(),
            'retail_under_test.c': GIVE_DOUBLES + strip_includes((ENGINE_SERVER / 'sv_cof_retail.c').read_text()),
        })
        self.assertIn('retail give scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
