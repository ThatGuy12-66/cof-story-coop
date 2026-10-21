/* sv_cof_pickups_state.h: who sees and takes which copy of a map's pickup, joiners, AI companions, what a command
 * or a frame saw, finding the map's pickups again, and the session ledger through level changes and checkpoints. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pickups_state_under_test.h"

static cof_pickup_ledger_t ledger;

static int kind_of( const char *classname )
{
	int row = CoF_PickupClass( classname );
	return row < 0 ? COF_PICKUP_NONE : cof_pickup_classes[row].kind;
}

static cof_pickup_look_t look( int gone, int owner, int follow, float nextthink )
{
	cof_pickup_look_t l;
	l.gone = gone; l.owner = owner; l.follow = follow; l.nextthink = nextthink;
	return l;
}

static int command_did( int kind, cof_pickup_look_t before, cof_pickup_look_t after )
{
	return CoF_PickupAfterCommand( kind, &before, &after );
}

static int frame_saw( cof_pickup_look_t seen, int maxclients, int *taker )
{
	return CoF_PickupAtFrame( &seen, maxclients, taker );
}

static cof_pickup_spot_t spot( const char *classname, float x, float y, float z )
{
	cof_pickup_spot_t s;
	s.classid = CoF_PickupCanonical( CoF_PickupClass( classname ));
	s.copy = 0;
	s.origin[0] = x; s.origin[1] = y; s.origin[2] = z;
	return s;
}

static void classes( void )
{
	/* the per-player classes the story maps place, with the kind of take each makes */
	static const char *weapons[] = { "weapon_syringe", "weapon_flare", "weapon_lantern", "weapon_branch", "weapon_nightstick",
		"weapon_m16", "weapon_tmp", "weapon_glock", "weapon_shotgun", "weapon_rifle", "weapon_vp70", "weapon_switchblade",
		"weapon_p345", "weapon_sledgehammer", "weapon_axe", "weapon_famas", "weapon_booklaser", "weapon_camera", "weapon_mp5" };
	static const char *ammo[] = { "ammo_glock", "ammo_m16", "ammo_p345", "ammo_revolver", "ammo_rifle", "ammo_shells", "ammo_tmp",
		"ammo_vp70", "ammo_flashlightbattery" };
	/* the story items stay with the share ledger; the starter lights, the chair's gun and what the game builds stay single */
	static const char *single[] = { "item_key", "item_phonebattery", "item_glocktaclight", "item_nightvision", "item_padlock",
		"weapon_mobile", "weapon_flashlight", "weapon_browning_wheelchair", "weapon_dualweapon", "weapon_action", "weapon_radio",
		"item_sodacan", "item_battery", "monstermaker", "", "WEAPON_SYRINGE" };
	int i;
	for( i = 0; i < (int)( sizeof( weapons ) / sizeof( weapons[0] )); i++ ) assert( kind_of( weapons[i] ) == COF_PICKUP_WEAPON );
	for( i = 0; i < (int)( sizeof( ammo ) / sizeof( ammo[0] )); i++ ) assert( kind_of( ammo[i] ) == COF_PICKUP_AMMO );
	assert( kind_of( "aom_pills" ) == COF_PICKUP_PILLS );
	for( i = 0; i < (int)( sizeof( single ) / sizeof( single[0] )); i++ ) assert( kind_of( single[i] ) == COF_PICKUP_NONE );
	assert( CoF_PickupClass( NULL ) < 0 );
	/* an alias is the class its Spawn renames it to */
	assert( CoF_PickupCanonical( CoF_PickupClass( "weapon_beretta" )) == CoF_PickupClass( "weapon_glock" ));
	assert( CoF_PickupCanonical( CoF_PickupClass( "weapon_P228" )) == CoF_PickupClass( "weapon_p345" ));
	assert( CoF_PickupCanonical( CoF_PickupClass( "weapon_mp5k" )) == CoF_PickupClass( "weapon_m16" ));
	assert( CoF_PickupCanonical( CoF_PickupClass( "ammo_glock" )) == CoF_PickupClass( "ammo_glock" ));
	assert( CoF_PickupCanonical( -1 ) == -1 );
	/* every row's canonical class is a row of its own kind */
	for( i = 0; i < COF_PICKUP_CLASSES; i++ )
		assert( CoF_PickupCanonical( i ) >= 0 && cof_pickup_classes[CoF_PickupCanonical( i )].kind == cof_pickup_classes[i].kind );

	/* a pickup a script names or whose take fires one stays single (its script runs once); so does a touch pickup */
	assert( CoF_PickupEligible( CoF_PickupClass( "weapon_shotgun" ), 0, 0, 0 ));
	assert( !CoF_PickupEligible( CoF_PickupClass( "weapon_shotgun" ), 1, 0, 0 ));
	assert( !CoF_PickupEligible( CoF_PickupClass( "weapon_shotgun" ), 0, 1, 0 ));
	assert( !CoF_PickupEligible( CoF_PickupClass( "weapon_shotgun" ), 0, 0, 1 ));
	assert( !CoF_PickupEligible( CoF_PickupClass( "item_key" ), 0, 0, 0 ));
}

static void names_and_origins( void )
{
	float o[3];
	assert( CoF_PickupName( "Simon" ) == CoF_PickupName( "simon" ) && CoF_PickupName( "Simon" ) != 0 );
	assert( CoF_PickupName( "Simon" ) != CoF_PickupName( "Simon (1)" ));
	assert( CoF_PickupName( "" ) == 0 && CoF_PickupName( NULL ) == 0 );
	assert( CoF_PickupParseOrigin( "-2312 -543.5 -1032", o ) && o[0] == -2312.0f && o[1] == -543.5f && o[2] == -1032.0f );
	assert( !CoF_PickupParseOrigin( "12 34", o ) && !CoF_PickupParseOrigin( "", o ) && !CoF_PickupParseOrigin( NULL, o ));
}

static void two_humans_a_joiner_and_a_companion( void )
{
	const unsigned int host = CoF_PickupName( "host" ), guest = CoF_PickupName( "guest" ), late = CoF_PickupName( "late" );
	unsigned int present[3];
	cof_pickup_takers_t t;
	memset( &t, 0, sizeof( t ));
	present[0] = host; present[1] = guest;

	/* the map's own syringe: everyone sees it, the humans can take it, an AI companion cannot */
	assert( !CoF_PickupHidden( &t, host ) && !CoF_PickupHidden( &t, guest ));
	assert( !CoF_PickupRefused( &t, host, 0 ) && !CoF_PickupRefused( &t, guest, 0 ) && CoF_PickupRefused( &t, 0, 1 ));

	/* the host takes it: a copy is wanted for the guest, which the host neither sees nor finds */
	assert( CoF_PickupSettle( &t, host, 0 ) && t.count == 1 );
	assert( CoF_PickupWanted( &t, present, 2 ));
	assert( CoF_PickupHidden( &t, host ) && !CoF_PickupHidden( &t, guest ));
	assert( CoF_PickupRefused( &t, host, 0 ) && !CoF_PickupRefused( &t, guest, 0 ) && CoF_PickupRefused( &t, 0, 1 ));

	/* the guest takes the copy: everyone present has one, no further copy */
	assert( CoF_PickupSettle( &t, guest, 0 ) && t.count == 2 );
	assert( !CoF_PickupWanted( &t, present, 2 ));

	/* a player who joins later gets theirs, hidden from the two who took one */
	present[2] = late;
	assert( CoF_PickupWanted( &t, present, 3 ));
	assert( !CoF_PickupHidden( &t, late ) && CoF_PickupHidden( &t, guest ));
	assert( CoF_PickupSettle( &t, late, 0 ) && !CoF_PickupWanted( &t, present, 3 ));

	/* a guest who left is not waited for; one back under the same name already has theirs */
	assert( !CoF_PickupWanted( &t, present, 1 ));

	/* an AI companion's take (a filter missed) takes nothing from the humans: their copy is still wanted */
	memset( &t, 0, sizeof( t ));
	assert( CoF_PickupSettle( &t, 0, 1 ) && t.count == 0 && CoF_PickupWanted( &t, present, 2 ));

	/* a human taking a copy hidden from them (a filter missed) ends the copies: no copy after copy */
	assert( CoF_PickupSettle( &t, host, 0 ));
	assert( !CoF_PickupSettle( &t, host, 0 ) && t.count == 1 );
	/* the list full: no more */
	memset( &t, 0, sizeof( t ));
	{
		char name[16];
		int i;
		for( i = 0; i < COF_PICKUP_TAKERS; i++ )
		{
			sprintf( name, "player%d", i );
			assert( CoF_PickupSettle( &t, CoF_PickupName( name ), 0 ));
		}
		assert( !CoF_PickupSettle( &t, late, 0 ) && t.count == COF_PICKUP_TAKERS );
	}
	/* nobody present: nothing wanted */
	memset( &t, 0, sizeof( t ));
	assert( !CoF_PickupWanted( &t, present, 0 ));
}

static void what_a_command_did( void )
{
	cof_pickup_look_t lying = look( 0, 0, 0, 0.0f );
	cof_pickup_look_t falling = look( 0, 0, 0, 12.3f );
	/* a new gun attached to its taker */
	assert( command_did( COF_PICKUP_WEAPON, lying, look( 0, 2, 1, 50.1f )) == COF_PICKUP_TAKEN );
	/* a gun the taker had: its ammo taken, SUB_Remove scheduled (the flare's for now) */
	assert( command_did( COF_PICKUP_WEAPON, lying, look( 0, 0, 0, 50.1f )) == COF_PICKUP_TAKEN );
	assert( command_did( COF_PICKUP_WEAPON, falling, look( 0, 0, 0, 50.0f )) == COF_PICKUP_TAKEN );
	/* ammo taken: SUB_Remove scheduled */
	assert( command_did( COF_PICKUP_AMMO, lying, look( 0, 0, 0, 50.1f )) == COF_PICKUP_TAKEN );
	/* full inventory, full ammo, a lantern already carried, a donator-only gun: nothing changed */
	assert( command_did( COF_PICKUP_WEAPON, lying, lying ) == COF_PICKUP_LYING );
	assert( command_did( COF_PICKUP_AMMO, falling, falling ) == COF_PICKUP_LYING );
	/* removed at once in a command: a locked bonus room gun or TMP ammo for a non-donator, on a first update */
	assert( command_did( COF_PICKUP_WEAPON, lying, look( 1, 0, 0, 0.0f )) == COF_PICKUP_GONE );
	assert( command_did( COF_PICKUP_AMMO, lying, look( 1, 0, 0, 0.0f )) == COF_PICKUP_GONE );
	/* pills remove themselves when they heal; at full health they stay */
	assert( command_did( COF_PICKUP_PILLS, lying, look( 1, 0, 0, 0.0f )) == COF_PICKUP_TAKEN );
	assert( command_did( COF_PICKUP_PILLS, lying, lying ) == COF_PICKUP_LYING );
}

static void what_a_frame_saw( void )
{
	int taker = -1;
	/* a falling gun's think, or nothing: still lying */
	assert( frame_saw( look( 0, 0, 0, 12.4f ), 4, &taker ) == COF_PICKUP_LYING && taker == 0 );
	/* on a player, however it got there: theirs */
	assert( frame_saw( look( 0, 3, 1, 12.4f ), 4, &taker ) == COF_PICKUP_TAKEN && taker == 3 );
	/* removed by the world, or held by something else: gone, no copy */
	assert( frame_saw( look( 1, 0, 0, 0.0f ), 4, &taker ) == COF_PICKUP_GONE && taker == 0 );
	assert( frame_saw( look( 0, 9, 1, 0.0f ), 4, &taker ) == COF_PICKUP_GONE );
	assert( frame_saw( look( 0, 0, 1, 0.0f ), 4, &taker ) == COF_PICKUP_GONE );
}

static void finding_the_maps_pickups( void )
{
	cof_pickup_spot_t blocks[4], lying[5];
	int match[4], forget = -1;
	/* c_apartment5's three ammo boxes side by side, a syringe, and the guns in the world */
	blocks[0] = spot( "ammo_glock", 100, 0, 40 );
	blocks[1] = spot( "ammo_glock", 108, 0, 40 );
	blocks[2] = spot( "ammo_glock", 116, 0, 40 );
	blocks[3] = spot( "weapon_syringe", 0, 0, 64 );
	lying[0] = spot( "ammo_glock", 116, 0, 36 );   /* fell 4 units */
	lying[1] = spot( "weapon_syringe", 0, 0, 0 );  /* fell to the floor 64 below */
	lying[2] = spot( "ammo_glock", 100, 0, 36 );
	lying[3] = spot( "ammo_glock", 300, 0, 36 );   /* a monster's drop elsewhere */
	lying[4] = spot( "ammo_m16", 108, 0, 36 );     /* another class where the middle box was */
	CoF_PickupMatch( blocks, 4, lying, 5, match );
	assert( match[0] == 2 && match[2] == 0 && match[3] == 1 );
	assert( match[1] == -1 ); /* the middle box was taken: none of its class lies there */
	/* too far below, beside or above: not this block's */
	blocks[0] = spot( "weapon_axe", 0, 0, 300 );
	lying[0] = spot( "weapon_axe", 0, 0, 0 );
	lying[1] = spot( "weapon_axe", 20, 0, 290 );
	lying[2] = spot( "weapon_axe", 0, 0, 320 );
	CoF_PickupMatch( blocks, 1, lying, 3, match );
	assert( match[0] == -1 );
	/* the one just taken (removed a tenth of a second later) and its copy at the same spot: the copy is the block's */
	blocks[0] = spot( "ammo_glock", 100, 0, 40 );
	lying[0] = spot( "ammo_glock", 100, 0, 36 );
	lying[1] = spot( "ammo_glock", 100, 0, 36 ); lying[1].copy = 1;
	CoF_PickupMatch( blocks, 1, lying, 2, match );
	assert( match[0] == 1 );
	lying[0].copy = 1; lying[1].copy = 0;
	CoF_PickupMatch( blocks, 1, lying, 2, match );
	assert( match[0] == 0 );
	/* an alias block finds the gun its Spawn renamed */
	blocks[0] = spot( "weapon_beretta", 0, 0, 10 );
	lying[0] = spot( "weapon_glock", 0, 0, 0 );
	CoF_PickupMatch( blocks, 1, lying, 1, match );
	assert( match[0] == 0 );

	/* the map starts: the map's own pickup lying was taken by nobody in this world */
	assert( CoF_PickupResolve( 1, 0, 0, 0, &forget ) == COF_PICKUP_LIVE && !forget );
	assert( CoF_PickupResolve( 1, 0, 2, 0, &forget ) == COF_PICKUP_LIVE && forget );
	/* a copy lying keeps the ledger's takers (none known: anyone's) */
	assert( CoF_PickupResolve( 1, 1, 1, 0, &forget ) == COF_PICKUP_LIVE && !forget );
	assert( CoF_PickupResolve( 1, 1, 0, 0, &forget ) == COF_PICKUP_LIVE && !forget );
	/* none lying: taken by some, waiting for the others; removed by the world or never taken: nothing */
	assert( CoF_PickupResolve( 0, 0, 1, 0, &forget ) == COF_PICKUP_WAITING && !forget );
	assert( CoF_PickupResolve( 0, 0, 1, 1, &forget ) == COF_PICKUP_OFF );
	assert( CoF_PickupResolve( 0, 0, 0, 0, &forget ) == COF_PICKUP_OFF );
}

static void the_ledger( void )
{
	const unsigned int host = CoF_PickupName( "host" ), guest = CoF_PickupName( "guest" );
	int start, sewer, a, b, c, i;
	memset( &ledger, 0, sizeof( ledger ));
	start = CoF_PickupLedgerMap( &ledger, "c_start", 1 );
	sewer = CoF_PickupLedgerMap( &ledger, "c_sewer2", 1 );
	assert( start == 0 && sewer == 1 && CoF_PickupLedgerMap( &ledger, "C_START", 0 ) == 0 && CoF_PickupLedgerMap( &ledger, "c_city", 0 ) == -1 );
	assert( CoF_PickupLedgerMap( &ledger, "", 1 ) == -1 && CoF_PickupLedgerMap( &ledger, NULL, 1 ) == -1 );
	assert( CoF_PickupLedgerFind( &ledger, start, 836, 0 ) == -1 && CoF_PickupLedgerFind( &ledger, -1, 836, 1 ) == -1 );
	a = CoF_PickupLedgerFind( &ledger, start, 836, 1 );
	assert( a == 0 && CoF_PickupLedgerFind( &ledger, start, 836, 1 ) == 0 && ledger.entries[a].saved == -1 );

	/* level changes keep it: the host's take of c_start's FAMAS is there on a revisit */
	CoF_PickupSettle( &ledger.entries[a].takers, host, 0 );
	b = CoF_PickupLedgerFind( &ledger, sewer, 417, 1 );
	CoF_PickupSettle( &ledger.entries[b].takers, guest, 0 );

	/* a checkpoint: then the guest takes the FAMAS copy, the host the sewer syringe's, a new pickup is taken, and
	 * one copy is removed by the world */
	CoF_PickupLedgerSaved( &ledger, "autosave" );
	CoF_PickupSettle( &ledger.entries[a].takers, guest, 0 );
	CoF_PickupSettle( &ledger.entries[b].takers, host, 0 );
	c = CoF_PickupLedgerFind( &ledger, sewer, 567, 1 );
	CoF_PickupSettle( &ledger.entries[c].takers, guest, 0 );
	ledger.entries[b].retired = 1;
	assert( ledger.count == 3 );

	/* the team wipes, the checkpoint loads: what it knew then (the guest can take the FAMAS copy again: the take was
	 * undone with the inventory; the host still cannot) */
	assert( CoF_PickupLedgerLoaded( &ledger, "autosave" ));
	assert( ledger.count == 2 );
	assert( CoF_PickupLedgerFind( &ledger, sewer, 567, 0 ) == -1 );
	a = CoF_PickupLedgerFind( &ledger, start, 836, 0 );
	b = CoF_PickupLedgerFind( &ledger, sewer, 417, 0 );
	assert( CoF_PickupTook( &ledger.entries[a].takers, host ) && !CoF_PickupTook( &ledger.entries[a].takers, guest ));
	assert( CoF_PickupTook( &ledger.entries[b].takers, guest ) && !CoF_PickupTook( &ledger.entries[b].takers, host ) && !ledger.entries[b].retired );
	/* and again after a second wipe */
	CoF_PickupSettle( &ledger.entries[a].takers, guest, 0 );
	assert( CoF_PickupLedgerLoaded( &ledger, "autosave" ) && ledger.entries[a].takers.count == 1 );

	/* a checkpoint it did not see written (another slot; Continue after a restart): it forgets everything */
	assert( !CoF_PickupLedgerLoaded( &ledger, "manual" ) && ledger.count == 0 && !ledger.saved_slot[0] );
	CoF_PickupLedgerFind( &ledger, start, 836, 1 );
	assert( !CoF_PickupLedgerLoaded( &ledger, "autosave" ) && ledger.count == 0 );
	/* a slot name too long to keep is not trusted */
	CoF_PickupLedgerSaved( &ledger, "a-slot-name-much-too-long-to-keep-here" );
	assert( !ledger.saved_slot[0] );

	/* full: no room for another pickup, nor another map */
	memset( &ledger, 0, sizeof( ledger ));
	for( i = 0; i < COF_PICKUP_LEDGER; i++ ) assert( CoF_PickupLedgerFind( &ledger, 0, i, 1 ) == i );
	assert( CoF_PickupLedgerFind( &ledger, 0, COF_PICKUP_LEDGER, 1 ) == -1 );
	for( i = 0; i < COF_PICKUP_MAPS; i++ )
	{
		char name[16];
		sprintf( name, "map%d", i );
		assert( CoF_PickupLedgerMap( &ledger, name, 1 ) == i );
	}
	assert( CoF_PickupLedgerMap( &ledger, "one_more", 1 ) == -1 );
}

int main( void )
{
	classes();
	names_and_origins();
	two_humans_a_joiner_and_a_companion();
	what_a_command_did();
	what_a_frame_saw();
	finding_the_maps_pickups();
	the_ledger();
	printf( "pickups state scenarios passed\n" );
	return 0;
}
