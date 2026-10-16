/* What other players see in a story co-op player's hands (sv_cof_weaponmodel.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "weaponmodel_under_test.h"

/* The models this map precached: a NULL-terminated list. */
static int Have( const char *path, void *ctx )
{
	const char **list = ctx;
	for( ; *list; list++ ) if( !strcmp( *list, path )) return 1;
	return 0;
}

static const char *Pick( const char *game, const char *active, int right, int left, const char **have )
{
	return CoF_WeaponModelPick( game, active, right, left, Have, (void *)have );
}

static void Expect( const char *got, const char *want )
{
	if( !want ) { if( got ) { printf( "got %s, want none\n", got ); assert( 0 ); } return; }
	if( !got || strcmp( got, want )) { printf( "got %s, want %s\n", got ? got : "(none)", want ); assert( 0 ); }
}

int main( void )
{
	const char *all[] = {
		"models/coop/p_axe.mdl", "models/coop/p_famas.mdl", "models/coop/p_mobile.mdl", "models/coop/p_rifle.mdl",
		"models/coop/p_lantern.mdl", "police/models/weapons/glock/p_glock.mdl", "police/models/weapons/m16/p_m16.mdl",
		"police/models/weapons/radio/p_radio.mdl", "police/models/weapons/rifle/p_rifle.mdl",
		"police/models/weapons/switchblade/p_switchblade.mdl", "models/weapons/flashlight/p_flashlight.mdl", NULL };
	const char *retail[] = { /* an old copy: no generated models */
		"police/models/weapons/glock/p_glock.mdl", "police/models/weapons/m16/p_m16.mdl",
		"police/models/weapons/radio/p_radio.mdl", "police/models/weapons/rifle/p_rifle.mdl", NULL };
	const char *none[] = { NULL };

	/* a story item the game shows as null.mdl (or nothing) gets the generated model */
	Expect( Pick( "models/null.mdl", "weapon_axe", 0, 0, all ), "models/coop/p_axe.mdl" );
	Expect( Pick( "", "weapon_axe", 0, 0, all ), "models/coop/p_axe.mdl" );
	Expect( Pick( "models/null.mdl", "weapon_lantern", 0, 0, all ), "models/coop/p_lantern.mdl" );
	/* without the generated model: a retail stand-in where one looks right, else empty hands */
	Expect( Pick( "models/null.mdl", "weapon_famas", 0, 0, retail ), "police/models/weapons/m16/p_m16.mdl" );
	Expect( Pick( "models/null.mdl", "weapon_mobile", 0, 0, retail ), "police/models/weapons/radio/p_radio.mdl" );
	Expect( Pick( "models/null.mdl", "weapon_axe", 0, 0, retail ), NULL );
	Expect( Pick( "models/null.mdl", "weapon_famas", 0, 0, none ), NULL );
	/* a weapon with its own model keeps it, unless Simon needs the grip-corrected copy */
	Expect( Pick( "police/models/weapons/glock/p_glock.mdl", "weapon_glock", 0, 0, all ), NULL );
	Expect( Pick( "police/models/weapons/rifle/p_rifle.mdl", "weapon_rifle", 0, 0, all ), "models/coop/p_rifle.mdl" );
	Expect( Pick( "police/models/weapons/rifle/p_rifle.mdl", "weapon_rifle", 0, 0, retail ), NULL );
	/* slashes and case as the game may write them */
	Expect( Pick( "police\\models\\weapons\\RIFLE\\p_rifle.mdl", "weapon_rifle", 0, 0, all ), "models/coop/p_rifle.mdl" );
	/* dual wield: the right hand's item, by weapon id */
	Expect( Pick( "models/null.mdl", "weapon_dualweapon", 3, 1, all ), "police/models/weapons/glock/p_glock.mdl" );
	Expect( Pick( "models/null.mdl", "weapon_dualweapon", 6, 2, all ), "police/models/weapons/switchblade/p_switchblade.mdl" );
	Expect( Pick( "models/null.mdl", "weapon_dualweapon", 99, 1, all ), NULL );
	Expect( Pick( "models/null.mdl", "weapon_dualweapon", 6, 2, none ), NULL );
	/* the other hand of a dual pair */
	Expect( CoF_WeaponModelOffhand( "weapon_dualweapon", 3, 1, Have, (void *)all ), NULL ); /* no l_ model yet */
	{
		const char *left[] = { "models/coop/l_flashlight.mdl", "models/coop/l_mobile.mdl", NULL };
		Expect( CoF_WeaponModelOffhand( "weapon_dualweapon", 3, 1, Have, (void *)left ), "models/coop/l_flashlight.mdl" );
		Expect( CoF_WeaponModelOffhand( "weapon_dualweapon", 6, 2, Have, (void *)left ), "models/coop/l_mobile.mdl" );
		Expect( CoF_WeaponModelOffhand( "weapon_glock", 3, 1, Have, (void *)left ), NULL );
	}
	/* nothing in hand, or an unknown item: the game's choice stands */
	Expect( Pick( "models/null.mdl", NULL, 0, 0, all ), NULL );
	Expect( Pick( "models/null.mdl", "weapon_actions", 0, 0, all ), NULL );
	/* every model the engine may precache is listed once, and short enough for a precache slot */
	{
		int i, j, n = CoF_WeaponModelCount();
		assert( n >= 10 );
		for( i = 0; i < n; i++ )
		{
			assert( strlen( CoF_WeaponModelPath( i )) < 64 );
			assert( !strncmp( CoF_WeaponModelPath( i ), "models/coop/", 12 ));
			for( j = 0; j < i; j++ ) assert( strcmp( CoF_WeaponModelPath( i ), CoF_WeaponModelPath( j )));
		}
		assert( CoF_WeaponModelPath( n ) == NULL );
	}
	printf( "weapon model scenarios passed\n" );
	return 0;
}
