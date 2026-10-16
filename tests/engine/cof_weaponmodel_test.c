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
	printf( "weapon model scenarios passed\n" );
	return 0;
}
