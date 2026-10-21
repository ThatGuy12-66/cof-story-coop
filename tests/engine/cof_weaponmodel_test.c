/* What other players see in a story co-op player's hands (sv_cof_weaponmodel.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "weaponmodel_under_test.h"

/* The models this map precached: a NULL-terminated list, or every model when NULL. */
static int Have( const char *path, void *ctx )
{
	const char **list = ctx;
	if( !list ) return 1;
	for( ; *list; list++ ) if( !strcmp( *list, path )) return 1;
	return 0;
}

static cof_weaponmodel_pick_t pick;

static int Pick( const char *game, int id, int right, int left, const char **have )
{
	return CoF_WeaponModelPick( game, id, right, left, Have, (void *)have, &pick );
}

static void Expect( const char *got, const char *want )
{
	if( !want ) { if( got ) { printf( "got %s, want none\n", got ); assert( 0 ); } return; }
	if( !got || strcmp( got, want )) { printf( "got %s, want %s\n", got ? got : "(none)", want ); assert( 0 ); }
}

int main( void )
{
	const char *retail[] = { /* an old copy: no generated models */
		"police/models/weapons/glock/p_glock.mdl", "police/models/weapons/m16/p_m16.mdl",
		"police/models/weapons/radio/p_radio.mdl", "police/models/weapons/rifle/p_rifle.mdl", NULL };
	const char *none[] = { NULL };
	int i;

	/* a story item the game shows as null.mdl gets the generated model and its pose */
	assert( Pick( "models/null.mdl", 24, 0, 0, NULL )); Expect( pick.model, "models/coop/p_axe.mdl" ); assert( pick.sequence == 50 );
	assert( Pick( "", 10, 0, 0, NULL )); Expect( pick.model, "models/coop/p_lantern.mdl" ); assert( pick.sequence == COF_WEAPONMODEL_KEEP );
	assert( Pick( "models/null.mdl", 23, 0, 0, NULL )); Expect( pick.model, "models/coop/p_famas.mdl" ); assert( pick.sequence == 36 );
	/* without the generated model: a retail look-alike where one looks right, in the game's own pose */
	assert( Pick( "models/null.mdl", 23, 0, 0, retail )); Expect( pick.model, "police/models/weapons/m16/p_m16.mdl" );
	assert( pick.sequence == COF_WEAPONMODEL_KEEP );
	assert( Pick( "models/null.mdl", 2, 0, 0, retail )); Expect( pick.model, "police/models/weapons/radio/p_radio.mdl" );
	assert( !Pick( "models/null.mdl", 24, 0, 0, retail ) && !pick.model );
	assert( !Pick( "models/null.mdl", 23, 0, 0, none ));
	/* the long guns Simon holds badly: the corrected copy, by id, whatever the game sends */
	assert( Pick( "police/models/weapons/rifle/p_rifle.mdl", 11, 0, 0, NULL )); Expect( pick.model, "models/coop/p_rifle.mdl" );
	assert( !Pick( "police/models/weapons/rifle/p_rifle.mdl", 11, 0, 0, retail ));
	assert( Pick( "police/models/weapons/tmp/p_tmp.mdl", 27, 0, 0, NULL )); Expect( pick.model, "models/coop/p_tmp.mdl" ); assert( pick.sequence == 32 );
	/* a weapon with a good model of its own keeps it */
	assert( !Pick( "police/models/weapons/glock/p_glock.mdl", 3, 0, 0, NULL ) && !pick.model );
	assert( !Pick( "police/models/weapons/m16/p_m16.mdl", 9, 0, 0, NULL ));
	/* dual wield: the light (second id) in the right hand, the pistol in the left, both arms forward */
	assert( Pick( "models/null.mdl", COF_WEAPONMODEL_DUAL, 1, 3, NULL ));
	Expect( pick.model, "models/coop/r_flashlight.mdl" ); Expect( pick.offhand, "models/coop/l_glock.mdl" );
	assert( pick.sequence == COF_WEAPONMODEL_DUAL_SEQUENCE );
	assert( Pick( "models/null.mdl", COF_WEAPONMODEL_DUAL, 3, 7, NULL ));
	Expect( pick.model, "police/models/weapons/glock/p_glock.mdl" ); Expect( pick.offhand, "models/coop/l_p345.mdl" );
	/* an old copy: the pistol in the right hand at least, no other hand */
	assert( Pick( "models/null.mdl", COF_WEAPONMODEL_DUAL, 3, 6, retail ));
	Expect( pick.model, "police/models/weapons/glock/p_glock.mdl" ); Expect( pick.offhand, NULL );
	assert( !Pick( "models/null.mdl", COF_WEAPONMODEL_DUAL, 1, 3, none ) && pick.sequence == COF_WEAPONMODEL_KEEP );
	assert( !Pick( "models/null.mdl", COF_WEAPONMODEL_DUAL, 99, 98, NULL ));
	/* nothing in hand, or an unknown item: the game's choice stands */
	assert( !Pick( "models/null.mdl", 0, 0, 0, NULL ));
	assert( !Pick( "models/null.mdl", 26, 0, 0, NULL ));
	assert( CoF_WeaponModelSame( "police\\models\\weapons\\RIFLE\\p_rifle.mdl", "police/models/weapons/rifle/p_rifle.mdl" ));
	/* 1.1.8 (E2-5): the pick's pose only while the player stands in it (deadflag, fuser3, crouched, walking,
	 * waterlevel, a game animation under way) */
	assert( CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 0, 0, 0 ));
	assert( CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 1, 0, 0 ));  /* wading */
	assert( !CoF_WeaponModelStanding( 2, 0.0f, 0, 1, 0, 0, 0 )); /* dead or downed */
	assert( !CoF_WeaponModelStanding( 0, 2.0f, 0, 1, 0, 0, 0 )); /* reviving, knocked down */
	assert( !CoF_WeaponModelStanding( 0, 1.0f, 0, 1, 0, 0, 0 )); /* tripped */
	assert( !CoF_WeaponModelStanding( 0, 0.0f, 1, 1, 0, 0, 0 )); /* crouched */
	assert( !CoF_WeaponModelStanding( 0, 0.0f, 0, 0, 0, 0, 0 )); /* on a ladder */
	assert( !CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 2, 0, 0 )); /* swimming */
	assert( !CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 0, 1, 0 )); /* the game's get-up, dodge... */
	/* 1.1.9 (movement 1): the game's sprint (flags bit 18 and over 10 units/s) keeps its own sequence */
	assert( !CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 0, 0, 1 ));
	assert( CoF_WeaponModelSprinting( 0x40000 | 0x200, 200.0f, 0.0f ));
	assert( CoF_WeaponModelSprinting( 0x40000, -8.0f, 8.0f ));  /* 11.3 diagonally */
	assert( !CoF_WeaponModelSprinting( 0x40000, 6.0f, 7.0f ));  /* 9.2: the game plays its idle */
	assert( !CoF_WeaponModelSprinting( 0x40000, 0.0f, 0.0f ));
	assert( !CoF_WeaponModelSprinting( 0x80000 | 0x200, 250.0f, 0.0f )); /* stamina (bit 19), not sprinting */
	assert( CoF_WeaponModelStanding( 0, 0.0f, 0, 1, 0, 0, CoF_WeaponModelSprinting( 0x80000, 250.0f, 0.0f )));
	/* 1.1.9: the gas mask goes out with the night vision fully on (flags bit 64), alive, drawn, off any ladder */
	assert( CoF_WeaponModelMasked( 64, 0, 0, 0 ));
	assert( CoF_WeaponModelMasked( 64 | 16 | 1, 0, 0, 0 ));
	assert( !CoF_WeaponModelMasked( 0, 0, 0, 0 ));    /* off, or only asked for (m_iHeadShieldOn 101) */
	assert( !CoF_WeaponModelMasked( 1, 0, 0, 0 ));    /* the flashlight */
	assert( !CoF_WeaponModelMasked( -1, 0, 0, 0 ));   /* no verified player */
	assert( !CoF_WeaponModelMasked( 64, 2, 0, 0 ));   /* dead */
	assert( !CoF_WeaponModelMasked( 64, 0, 1, 0 ));   /* not drawn */
	assert( !CoF_WeaponModelMasked( 64, 0, 0, 1 ));   /* on a ladder */
	/* the numbers sent with players: never an edict of the game, never one another's, for any party size */
	{
		static int seen[4096];
		int maxclients, kind, slot, max_entities = 4096;
		for( maxclients = 1; maxclients <= 32; maxclients++ )
		{
			memset( seen, 0, sizeof( seen ));
			for( kind = 0; kind < COF_WEAPONMODEL_KINDS; kind++ )
				for( slot = 0; slot < maxclients; slot++ )
				{
					int number = CoF_WeaponModelNumber( kind, slot, max_entities, maxclients, 900 );
					assert( number >= 900 && number < max_entities && number > maxclients );
					assert( !seen[number] );
					seen[number] = 1;
				}
			assert( !CoF_WeaponModelNumber( 0, maxclients, max_entities, maxclients, 900 ));
		}
		/* the 1.1.8 formulas: the other hand at max - 1 - slot, the held light max - 1 - maxclients - slot */
		assert( CoF_WeaponModelNumber( COF_WEAPONMODEL_OFFHAND, 2, 2048, 4, 600 ) == 2048 - 1 - 2 );
		assert( CoF_WeaponModelNumber( COF_WEAPONMODEL_LIGHT, 2, 2048, 4, 600 ) == 2048 - 1 - 4 - 2 );
		assert( CoF_WeaponModelNumber( COF_WEAPONMODEL_GASMASK, 2, 2048, 4, 600 ) == 2048 - 1 - 8 - 2 );
		/* the game reached the top: none */
		assert( !CoF_WeaponModelNumber( COF_WEAPONMODEL_GASMASK, 3, 2048, 4, 2048 - 12 + 1 ));
		assert( CoF_WeaponModelNumber( COF_WEAPONMODEL_GASMASK, 3, 2048, 4, 2048 - 12 ) == 2048 - 12 );
		assert( !CoF_WeaponModelNumber( COF_WEAPONMODEL_KINDS, 0, 2048, 4, 600 ));
		assert( !CoF_WeaponModelNumber( 0, -1, 2048, 4, 600 ));
		assert( !CoF_WeaponModelNumber( 2, 0, 10, 4, 1 )); /* would land on a player */
	}
	/* 1.1.9 (ladders): the climber's body: eye at the hands' view, facing along it, level */
	{
		const float eye[3] = { 100.0f, 50.0f, 300.0f }, ahead[3] = { 100.0f, 58.0f, 302.0f }, up[3] = { 100.0f, 50.0f, 310.0f };
		float origin[3], heading[2];
		assert( CoF_WeaponModelClimbPose( eye, ahead, 28.0f, origin, heading ));
		assert( origin[0] == 100.0f && origin[1] == 50.0f && origin[2] == 272.0f );
		assert( heading[0] == 0.0f && heading[1] == 8.0f ); /* yaw 90 */
		assert( !CoF_WeaponModelClimbPose( eye, up, 28.0f, origin, heading )); /* straight up: the model's yaw */
		assert( origin[2] == 272.0f );
	}
	/* 1.1.9 (P5): a rower's protection once out of the boat */
	assert( CoF_WeaponModelBoatLeft( 2, 1, 0, 0 ) == COF_BOAT_RESTORE );       /* docked */
	assert( CoF_WeaponModelBoatLeft( 2, 1, 1, 0 ) == COF_BOAT_WAIT );          /* a cutscene camera holds them */
	assert( CoF_WeaponModelBoatLeft( 1, 1, 0, 1 ) == COF_BOAT_WAIT );          /* loading the next map */
	assert( CoF_WeaponModelBoatLeft( 2, 1, 0, 1 ) == COF_BOAT_RESTORE_AGAIN ); /* let go for a level change */
	assert( CoF_WeaponModelBoatLeft( 0, 1, 0, 0 ) == COF_BOAT_RESTORE );       /* dropped */
	assert( CoF_WeaponModelBoatLeft( 2, 0, 0, 0 ) == COF_BOAT_FORGET );        /* someone else in the slot */
	assert( CoF_WeaponModelBoatLeft( 0, 0, 0, 1 ) == COF_BOAT_FORGET );
	/* every model the engine may precache is a generated one, short enough for a precache slot;
	 * empty entries (a retail right hand) are skipped by the caller */
	for( i = 0; i < CoF_WeaponModelCount(); i++ )
	{
		const char *path = CoF_WeaponModelPath( i );
		assert( path && strlen( path ) < 64 );
		assert( !path[0] || !strncmp( path, "models/coop/", 12 ));
	}
	assert( CoF_WeaponModelPath( CoF_WeaponModelCount()) == NULL );
	/* the table, for the generator's mapping (tests/test_coop_weaponmodel.py compares the two) */
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_single ); i++ )
		printf( "single %d %s %d\n", cof_weaponmodel_single[i].id, cof_weaponmodel_single[i].coop, cof_weaponmodel_single[i].sequence );
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual ); i++ )
		printf( "dual %d %s %s\n", cof_weaponmodel_dual[i].id, cof_weaponmodel_dual[i].right, cof_weaponmodel_dual[i].left );
	printf( "dualsequence %d\n", COF_WEAPONMODEL_DUAL_SEQUENCE );
	printf( "gasmask %s %d\n", COF_WEAPONMODEL_MASK, COF_WEAPONMODEL_MASK_ON );
	printf( "climbsequence %d\n", COF_WEAPONMODEL_CLIMB_SEQUENCE );
	assert( !strcmp( CoF_WeaponModelPath( CoF_WeaponModelCount() - 1 ), COF_WEAPONMODEL_MASK ));
	printf( "weapon model scenarios passed\n" );
	return 0;
}
