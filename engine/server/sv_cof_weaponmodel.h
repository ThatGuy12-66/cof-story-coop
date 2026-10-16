/* Cry of Fear story co-op: the weapon other players see in a player's hands. GPL-3.0-or-later.
 *
 * Retail co-op was made for the police campaign: only the police weapons have a
 * third-person ("p_") model, and the game's Deploy shows models/null.mdl for every
 * story-only item (axe, FAMAS, lantern, flare, phone, camera, book laser, Browning)
 * and for weapon_dualweapon, so the others see empty hands. The tester setup makes
 * stand-in models from the player's own game files (tools/prepare_coop_weapons.py,
 * models/coop/): the story items, grip-corrected long guns for the Simon models, and
 * both hands of a dual pair (the other hand is drawn by a follow entity merged onto
 * the player). Some items look right only in another body pose than the one the game
 * plays, so the pick can also name the sequence to send. The engine only changes the
 * state it sends; the game's own entity is kept as it is.
 * Research: evidence/tester-package-20261015/research/thirdperson-weapons-re.md,
 * evidence/weapons-companions-20261015/research/pmodels-report.md.
 *
 * Weapons are matched by m_iId (hl.dll GetItemInfo): classname aliases share it.
 * The table mirrors prepare_coop_weapons.engine_mapping(); a test compares the two.
 *
 * Pure: no engine headers; the offline harness compiles it as it is. */
#ifndef SV_COF_WEAPONMODEL_H
#define SV_COF_WEAPONMODEL_H
#include <stddef.h>
#include <string.h>

/* Whether this map has the model (precached). */
typedef int (*cof_weaponmodel_have_t)( const char *path, void *ctx );

#define COF_WEAPONMODEL_DUAL 15 /* weapon_dualweapon */
#define COF_WEAPONMODEL_DUAL_SEQUENCE 36 /* pistol_idle: both arms forward (the game plays idle, arms hanging) */
#define COF_WEAPONMODEL_KEEP -1

/* One item in hand: the generated model (and the pose it was made for), else a
 * retail look-alike when the game shows nothing, else the game's own. */
typedef struct { int id; const char *coop; int sequence; const char *retail; } cof_weaponmodel_single_t;
static const cof_weaponmodel_single_t cof_weaponmodel_single[] =
{
	{ 24, "models/coop/p_axe.mdl", 50, NULL }, /* sledgehammer_idle: raised like the other melee */
	{ 23, "models/coop/p_famas.mdl", 36, "police/models/weapons/m16/p_m16.mdl" }, /* shouldered */
	{ 10, "models/coop/p_lantern.mdl", COF_WEAPONMODEL_KEEP, NULL },
	{ 13, "models/coop/p_flare.mdl", COF_WEAPONMODEL_KEEP, NULL },
	{ 2, "models/coop/p_mobile.mdl", COF_WEAPONMODEL_KEEP, "police/models/weapons/radio/p_radio.mdl" },
	{ 5, "models/coop/p_camera.mdl", 55, NULL }, /* radio_idle: held out */
	{ 25, "models/coop/p_booklaser.mdl", 55, NULL },
	{ 20, "models/coop/p_browning.mdl", 36, NULL }, /* the wheelchair Browning */
	{ 11, "models/coop/p_rifle.mdl", COF_WEAPONMODEL_KEEP, NULL }, /* the stock one points 40 degrees up on Simon */
	{ 4, "models/coop/p_shotgun.mdl", COF_WEAPONMODEL_KEEP, NULL },
	{ 14, "models/coop/p_tmp.mdl", COF_WEAPONMODEL_KEEP, NULL },
	{ 27, "models/coop/p_tmp.mdl", 32, NULL }, /* the MP5 shows the TMP, and its own sequence is missing */
};

/* Dual wield: StartDualWield puts a light (or the second pistol) second, at
 * m_iDualWeaponRightID (P+0x1E74): Simon holds that one in his right hand, the
 * other (P+0x1E70) in his left (the client draws that one mirrored). */
typedef struct { int id; const char *right, *left; } cof_weaponmodel_dual_t;
static const cof_weaponmodel_dual_t cof_weaponmodel_dual[] =
{
	{ 1, "models/coop/r_flashlight.mdl", "models/coop/l_flashlight.mdl" },
	{ 2, "models/coop/p_mobile.mdl", "models/coop/l_mobile.mdl" },
	{ 3, "police/models/weapons/glock/p_glock.mdl", "models/coop/l_glock.mdl" },
	{ 6, "police/models/weapons/switchblade/p_switchblade.mdl", "models/coop/l_switchblade.mdl" },
	{ 7, "police/models/weapons/p345/p_p345.mdl", "models/coop/l_p345.mdl" },
	{ 10, "models/coop/r_lantern.mdl", "models/coop/l_lantern.mdl" },
	{ 12, "police/models/weapons/nightstick/p_nightstick.mdl", "models/coop/l_nightstick.mdl" },
	{ 13, "models/coop/p_flare.mdl", "models/coop/l_flare.mdl" },
	{ 16, "police/models/weapons/branch/p_branch.mdl", "models/coop/l_branch.mdl" },
	{ 19, "police/models/weapons/vp70/p_vp70.mdl", "models/coop/l_vp70.mdl" },
	{ 21, "police/models/weapons/revolver/p_revolver.mdl", "models/coop/l_revolver.mdl" },
};

#define COF_WEAPONMODEL_COUNT( a ) ( (int)( sizeof( a ) / sizeof( ( a )[0] )))

typedef struct
{
	const char *model; /* the weaponmodel to send, or NULL for the game's */
	int sequence; /* the body sequence to send, or COF_WEAPONMODEL_KEEP */
	const char *offhand; /* the other hand's model (a follow entity), or NULL */
} cof_weaponmodel_pick_t;

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

static const cof_weaponmodel_dual_t *CoF_WeaponModelDualItem( int id )
{
	int i;
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual ); i++ )
		if( cof_weaponmodel_dual[i].id == id ) return &cof_weaponmodel_dual[i];
	return NULL;
}

/* What others should see. game: the game's weaponmodel string ("" for none); id:
 * the held item's m_iId (0 for none); right, left: for the dual weapon, the item id
 * in each hand (P+0x1E74, P+0x1E70). Returns whether anything changes. */
static int CoF_WeaponModelPick( const char *game, int id, int right, int left,
	cof_weaponmodel_have_t have, void *ctx, cof_weaponmodel_pick_t *out )
{
	int i;
	out->model = out->offhand = NULL;
	out->sequence = COF_WEAPONMODEL_KEEP;
	if( id <= 0 ) return 0;
	if( id == COF_WEAPONMODEL_DUAL )
	{
		const cof_weaponmodel_dual_t *r = CoF_WeaponModelDualItem( right ), *l = CoF_WeaponModelDualItem( left );
		out->model = r ? CoF_WeaponModelHave( r->right, have, ctx ) : NULL;
		out->offhand = l ? CoF_WeaponModelHave( l->left, have, ctx ) : NULL;
		if( out->model || out->offhand ) out->sequence = COF_WEAPONMODEL_DUAL_SEQUENCE;
		return out->model || out->offhand;
	}
	for( i = 0; i < COF_WEAPONMODEL_COUNT( cof_weaponmodel_single ); i++ )
	{
		const cof_weaponmodel_single_t *item = &cof_weaponmodel_single[i];
		if( item->id != id ) continue;
		if(( out->model = CoF_WeaponModelHave( item->coop, have, ctx )))
		{
			out->sequence = item->sequence;
			return 1;
		}
		// no generated model: a look-alike, only where the game shows nothing
		if( !game || !game[0] || CoF_WeaponModelSame( game, "models/null.mdl" ))
			out->model = CoF_WeaponModelHave( item->retail, have, ctx );
		return out->model != NULL;
	}
	return 0;
}

/* Every generated model the engine precaches when the file exists, by index; NULL past the end. */
static int CoF_WeaponModelCount( void )
{
	return COF_WEAPONMODEL_COUNT( cof_weaponmodel_single ) + 2 * COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual );
}

static const char *CoF_WeaponModelGenerated( const char *path )
{
	return path && !strncmp( path, "models/coop/", 12 ) ? path : NULL;
}

static const char *CoF_WeaponModelPath( int index )
{
	int singles = COF_WEAPONMODEL_COUNT( cof_weaponmodel_single ), duals = COF_WEAPONMODEL_COUNT( cof_weaponmodel_dual );
	if( index < 0 ) return NULL;
	if( index < singles ) return cof_weaponmodel_single[index].coop;
	index -= singles;
	if( index < duals ) return CoF_WeaponModelGenerated( cof_weaponmodel_dual[index].right ) ? cof_weaponmodel_dual[index].right : "";
	index -= duals;
	if( index < duals ) return cof_weaponmodel_dual[index].left;
	return NULL;
}
#endif
