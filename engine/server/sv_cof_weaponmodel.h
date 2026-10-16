/* Cry of Fear story co-op: the weapon other players see in a player's hands. GPL-3.0-or-later.
 *
 * Retail co-op was made for the police campaign: only the police weapons have a
 * third-person ("p_") model, and the game's Deploy shows models/null.mdl for every
 * story-only item (axe, FAMAS, lantern, flare, phone, camera, book laser, Browning)
 * and for weapon_dualweapon, so the others see empty hands. The tester setup makes
 * stand-in models from the player's own game files (tools/prepare_coop_weapons.py,
 * models/coop/), including grip-corrected long guns for the Simon player models.
 * The engine only changes the model index it sends; the game's own state is kept.
 * Research: evidence/tester-package-20261015/research/thirdperson-weapons-re.md.
 *
 * Pure: no engine headers; the offline harness compiles it as it is. */
#ifndef SV_COF_WEAPONMODEL_H
#define SV_COF_WEAPONMODEL_H
#include <stddef.h>
#include <string.h>

/* Whether this map has the model (precached). */
typedef int (*cof_weaponmodel_have_t)( const char *path, void *ctx );

typedef struct { const char *classname, *coop, *retail; } cof_weaponmodel_item_t;

/* Story items the game shows as null.mdl: the generated model, then a retail one
 * that looks close enough, else nothing (empty hands, as retail). */
static const cof_weaponmodel_item_t cof_weaponmodel_items[] =
{
	{ "weapon_axe", "models/coop/p_axe.mdl", NULL },
	{ "weapon_famas", "models/coop/p_famas.mdl", "police/models/weapons/m16/p_m16.mdl" },
	{ "weapon_lantern", "models/coop/p_lantern.mdl", NULL },
	{ "weapon_flare", "models/coop/p_flare.mdl", NULL },
	{ "weapon_mobile", "models/coop/p_mobile.mdl", "police/models/weapons/radio/p_radio.mdl" },
	{ "weapon_camera", "models/coop/p_camera.mdl", NULL },
	{ "weapon_booklaser", "models/coop/p_booklaser.mdl", NULL },
	{ "weapon_browning", "models/coop/p_browning.mdl", NULL },
};

/* Retail models the Simon skeleton holds badly (the rifle points up, long guns at
 * the hip): the grip-corrected copy, when this map has it. */
static const struct { const char *retail, *coop; } cof_weaponmodel_swaps[] =
{
	{ "police/models/weapons/rifle/p_rifle.mdl", "models/coop/p_rifle.mdl" },
	{ "police/models/weapons/shotgun/p_shotgun.mdl", "models/coop/p_shotgun.mdl" },
	{ "police/models/weapons/m16/p_m16.mdl", "models/coop/p_m16.mdl" },
	{ "police/models/weapons/tmp/p_tmp.mdl", "models/coop/p_tmp.mdl" },
	{ "police/models/weapons/g43/p_g43.mdl", "models/coop/p_g43.mdl" },
};

/* The items dual wield allows (hl.dll CanDualWield 0x100DC020), by m_iId: the
 * right hand's model and the left hand's generated variant. */
static const struct { int id; const char *classname, *right, *left; } cof_weaponmodel_dual[] =
{
	{ 1, "weapon_flashlight", "models/weapons/flashlight/p_flashlight.mdl", "models/coop/l_flashlight.mdl" },
	{ 2, "weapon_mobile", NULL, "models/coop/l_mobile.mdl" },
	{ 3, "weapon_glock", "police/models/weapons/glock/p_glock.mdl", "models/coop/l_glock.mdl" },
	{ 6, "weapon_switchblade", "police/models/weapons/switchblade/p_switchblade.mdl", "models/coop/l_switchblade.mdl" },
	{ 7, "weapon_p345", "police/models/weapons/p345/p_p345.mdl", "models/coop/l_p345.mdl" },
	{ 10, "weapon_lantern", NULL, "models/coop/l_lantern.mdl" },
	{ 12, "weapon_nightstick", "police/models/weapons/nightstick/p_nightstick.mdl", "models/coop/l_nightstick.mdl" },
	{ 13, "weapon_flare", NULL, "models/coop/l_flare.mdl" },
	{ 16, "weapon_branch", "police/models/weapons/branch/p_branch.mdl", "models/coop/l_branch.mdl" },
	{ 19, "weapon_vp70", "police/models/weapons/vp70/p_vp70.mdl", "models/coop/l_vp70.mdl" },
	{ 21, "weapon_revolver", "police/models/weapons/revolver/p_revolver.mdl", "models/coop/l_revolver.mdl" },
};

#define COF_WEAPONMODEL_COUNT( a ) ( (int)( sizeof( a ) / sizeof( ( a )[0] )))

/* Path equality as the engine compares precache names: case and slash blind. */
static int CoF_WeaponModelSame( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
	{
		char x = *a == '\\' ? '/' : ( *a >= 'A' && *a <= 'Z' ) ? (char)( *a + 32 ) : *a;
		char y = *b == '\\' ? '/' : ( *b >= 'A' && *b <= 'Z' ) ? (char)( *b + 32 ) : *b;
		if( x != y ) return 0;
	}
	return !*a && !*b;
}

static const char *CoF_WeaponModelHave( const char *path, cof_weaponmodel_have_t have, void *ctx )
{
	return path && have( path, ctx ) ? path : NULL;
}

/* A story item's stand-in: the generated model, else the retail look-alike, else NULL. */
static const char *CoF_WeaponModelStory( const char *classname, cof_weaponmodel_have_t have, void *ctx )
{
	int i;
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_items ); i++ )
	{
		const cof_weaponmodel_item_t *item = &cof_weaponmodel_items[i];
		const char *path;
		if( strcmp( item->classname, classname )) continue;
		if(( path = CoF_WeaponModelHave( item->coop, have, ctx ))) return path;
		return CoF_WeaponModelHave( item->retail, have, ctx );
	}
	return NULL;
}

/* The model others should see, or NULL to keep the game's. game: the game's
 * weaponmodel string ("" for none); active: the held item's classname (NULL for
 * none); right/left: for weapon_dualweapon, the weapon ids in each hand. */
static const char *CoF_WeaponModelPick( const char *game, const char *active, int right, int left,
	cof_weaponmodel_have_t have, void *ctx )
{
	int i;
	(void)left;
	if( !active ) return NULL;
	if( !strcmp( active, "weapon_dualweapon" ))
	{
		for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual ); i++ )
			if( cof_weaponmodel_dual[i].id == right )
			{
				const char *path = CoF_WeaponModelHave( cof_weaponmodel_dual[i].right, have, ctx );
				return path ? path : CoF_WeaponModelStory( cof_weaponmodel_dual[i].classname, have, ctx );
			}
		return NULL;
	}
	if( !game || !game[0] || CoF_WeaponModelSame( game, "models/null.mdl" ))
		return CoF_WeaponModelStory( active, have, ctx );
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_swaps ); i++ )
		if( CoF_WeaponModelSame( game, cof_weaponmodel_swaps[i].retail ))
			return CoF_WeaponModelHave( cof_weaponmodel_swaps[i].coop, have, ctx );
	return NULL;
}

/* The other hand of a dual pair: the left-hand variant of the left item, or NULL. */
static const char *CoF_WeaponModelOffhand( const char *active, int right, int left,
	cof_weaponmodel_have_t have, void *ctx )
{
	int i;
	(void)right;
	if( !active || strcmp( active, "weapon_dualweapon" )) return NULL;
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual ); i++ )
		if( cof_weaponmodel_dual[i].id == left )
			return CoF_WeaponModelHave( cof_weaponmodel_dual[i].left, have, ctx );
	return NULL;
}

/* Every generated model the engine precaches when the file exists, by index; NULL past the end. */
static int CoF_WeaponModelCount( void )
{
	return COF_WEAPONMODEL_COUNT( cof_weaponmodel_items ) + COF_WEAPONMODEL_COUNT( cof_weaponmodel_swaps ) +
		COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual );
}

static const char *CoF_WeaponModelPath( int index )
{
	if( index < 0 ) return NULL;
	if( index < COF_WEAPONMODEL_COUNT( cof_weaponmodel_items )) return cof_weaponmodel_items[index].coop;
	index -= COF_WEAPONMODEL_COUNT( cof_weaponmodel_items );
	if( index < COF_WEAPONMODEL_COUNT( cof_weaponmodel_swaps )) return cof_weaponmodel_swaps[index].coop;
	index -= COF_WEAPONMODEL_COUNT( cof_weaponmodel_swaps );
	if( index < COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual )) return cof_weaponmodel_dual[index].left;
	return NULL;
}
#endif
