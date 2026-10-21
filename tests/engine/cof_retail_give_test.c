/* The story item give through the retail layer (1.1.8): the taclight and the phone battery are CItems, which the
 * game hands over only on a fresh +use press (items.cpp CGlockTacLight/CPhoneBattery MyTouch: m_afButtonPressed &
 * IN_USE). The retail harness's doubles are reused; its descriptor files and its give are replaced by the ones
 * below (retail_under_test.c starts with them, see tests/test_enhanced_retail_give.py). */
#define FS_LoadFile fixture_FS_LoadFile
#define SV_CoF_StoryGiveItemEx fixture_StoryGiveItemEx
#define main retail_fixture_main
#include "retail_fixture.c"
#undef main

int main( void )
{
	int pressed, i;
	svs.maxclients = 2;
	svs.clients = clients;
	svgame.dllFuncs.pfnClientCommand = ClientCommand;
	svgame.edicts = edicts;
	svgame.numEntities = 6;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i + 1];
		edicts[i + 1].pvPrivateData = private_data[i];
	}

	/* the taclight: given with +use freshly pressed, the player's own presses kept and put back after */
	pressed = 2; /* IN_JUMP, pressed by the player this frame */
	memcpy( private_data[1] + 0x22B0, &pressed, sizeof( pressed ));
	assert( SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/taclight.txt" ));
	assert( !strcmp( give_seen_class, "item_glocktaclight" ) && give_seen_pressed == ( 2 | 32 ));
	memcpy( &pressed, private_data[1] + 0x22B0, sizeof( pressed ));
	assert( pressed == 2 );
	/* the phone battery the same way */
	pressed = 0; memcpy( private_data[1] + 0x22B0, &pressed, sizeof( pressed ));
	assert( SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/mobile_battery.txt" ));
	assert( !strcmp( give_seen_class, "item_phonebattery" ) && give_seen_pressed == 32 );
	memcpy( &pressed, private_data[1] + 0x22B0, sizeof( pressed ));
	assert( pressed == 0 );
	/* a key is unchanged (KeyitemUse looks at no button); a refused give still puts the presses back */
	assert( SV_CoF_RetailGiveItem( &clients[0], "inventoryitems/hallkey.txt" ) && !strcmp( give_seen_class, "item_key" ));
	give_refuse = 1;
	assert( !SV_CoF_RetailGiveItem( &clients[0], "inventoryitems/hallkey.txt" ));
	memcpy( &pressed, private_data[0] + 0x22B0, sizeof( pressed ));
	assert( pressed == 0 );
	/* nobody spawned: nothing given, nothing pressed */
	give_calls = 0; clients[1].state = 2;
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/taclight.txt" ) && !give_calls );
	puts( "retail give scenarios passed" );
	return 0;
}
