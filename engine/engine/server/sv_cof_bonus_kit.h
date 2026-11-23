/* Cry of Fear story co-op: the bonus room's items at a chapter start (sv_cof_bonus.c). GPL-3.0-or-later.
 *
 * The bonus room is in c_start (Chapter 1) and c_bridge (Chapter 6) only: inter_door `unlockablesdoor` (c_start #828,
 * c_bridge #377) leads to `uex2` in a sealed room holding the FAMAS, the axe, the MP5, the book laser, the camera and
 * the night vision (and the wardrobe; c_start also the package key, left out here: a key is a story item). A party
 * that starts a later chapter from the app's chapter list never passed one. When the room is open for this game
 * (cof_bonus_room, sv_cof_bonus.c), such a start lays the room's items out on the floor beside where the party
 * starts, once (the app names the chapter in cof_chapter_start for that one start only). Each lies with the room's
 * own keys, so the game's per-item unlock check (each item's SendInitMessage reading scriptsettings.dat through the
 * server's LoadFileForMe) still decides; per-player copies are sv_cof_pickups.c's (the bonus mark).
 *
 * Pure: no engine headers; the offline harness drives it as it is. */
#ifndef SV_COF_BONUS_KIT_H
#define SV_COF_BONUS_KIT_H
#include <stddef.h>
#include <string.h>

#define COF_BONUS_KIT_ITEMS 6
#define COF_BONUS_KIT_KEYS 18 /* key and value strings, NULL-ended */

typedef struct
{
	const char *classname;
	const char *keys[COF_BONUS_KIT_KEYS]; /* the room's keys but origin and angles (c_start's entity text) */
	float yaw;                            /* as it lies in the room */
	int gate;                             /* its line in cof_bonus_lines (sv_cof_bonus.h); 0: the game's other rule */
	int item;                             /* 1: a CItem, taken by touch with +use pressed and removed at once */
} cof_bonus_kit_item_t;

/* c_start's room (#836, #867, #895, #896, #884, #934); c_bridge's differ only in the MP5's ammo (frags 180). The MP5
 * last: with the game the room's own MP5 removed itself for anyone without donator level 2-4 (CMp5::SendInitMessage),
 * leaving the last spot empty, which the mp5-init gates (sv_cof_gates.h, cof_coop_mp5) now keep it from. */
static const cof_bonus_kit_item_t cof_bonus_kit[COF_BONUS_KIT_ITEMS] =
{
	{ "weapon_famas", { "rendercolor", "0 0 0", "frags", "-1", NULL }, 56.0f, 39, 0 },
	{ "weapon_axe", { "rendercolor", "0 0 0", "frags", "-1", NULL }, 146.0f, 70, 0 },
	{ "weapon_booklaser", { "renderamt", "0", "rendermode", "0", "renderfx", "0", "delay", "0", "skill", "0",
		"rendercolor", "0 0 0", "frags", "-1", NULL }, -163.0f, 72, 0 },
	{ "weapon_camera", { "spawnflags", "1", "renderamt", "0", "rendermode", "0", "renderfx", "0", "delay", "0",
		"skill", "0", "rendercolor", "0 0 0", "frags", "-1", NULL }, 42.0f, 71, 0 },
	{ "item_nightvision", { "frags", "-1", NULL }, 37.0f, 75, 1 },
	{ "weapon_mp5", { "rendercolor", "0 0 0", "frags", "330", NULL }, 40.0f, 0, 0 },
};

/* The kit's item of this class, or -1. */
static int CoF_BonusKitItem( const char *classname )
{
	int i;
	if( !classname ) return -1;
	for( i = 0; i < COF_BONUS_KIT_ITEMS; i++ )
		if( !strcmp( cof_bonus_kit[i].classname, classname )) return i;
	return -1;
}

static int CoF_BonusKitSameName( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
	{
		char x = *a, y = *b;
		if( x >= 'A' && x <= 'Z' ) x = (char)( x + 32 );
		if( y >= 'A' && y <= 'Z' ) y = (char)( y + 32 );
		if( x != y ) return 0;
	}
	return *a == *b;
}

/* A chapter start (the map cof_chapter_start names) that gets the kit: every one but c_bridge, which has the room
 * itself, and c_start (Chapter 1), which has it too; nor c_nightmare (the camera prologue): it leads straight into
 * c_start, and the client turns the inventory off by its name while c_start's cof_clearitems takes a camera back. */
static int CoF_BonusKitWanted( const char *map )
{
	static const char *own[] = { "c_start", "c_bridge", "c_nightmare" };
	int i;
	if( !map || !map[0] ) return 0;
	for( i = 0; i < (int)( sizeof( own ) / sizeof( own[0] )); i++ )
		if( CoF_BonusKitSameName( map, own[i] )) return 0;
	return 1;
}

/* Where the party starts: the game puts a new player at the first info_player_start (EntSelectSpawnPoint, player.cpp
 * 7116-7121; the story maps have no cof_coop_spawnN or info_player_deathmatch). In a merged map the chapter's own
 * section's first one (sv_cof_sections.c SV_CoF_SectionsEntry makes it the first): sections[i] is the section of the
 * i-th info_player_start in entity order, wanted the chapter's (-1: the map is not merged). The index, -1 for none. */
static int CoF_BonusKitStart( const int *sections, int count, int wanted )
{
	int i;
	if( count <= 0 ) return -1;
	for( i = 0; wanted >= 0 && i < count; i++ )
		if( sections[i] == wanted ) return i;
	return 0;
}

/* The checks a spot passes, as the engine answers them (sv_cof_bonus.c: traces against the world and brush entities,
 * the map's triggers, the merged map's sections). */
typedef struct
{
	void *ctx;
	int ( *reach )( void *ctx, const float *from, const float *to );  /* a straight line between them crosses no wall */
	int ( *ground )( void *ctx, const float *point, float *z );      /* the floor below point within reach, its height */
	int ( *stand )( void *ctx, const float *feet );                  /* a player fits standing there, out of water */
	int ( *danger )( void *ctx, const float *feet );                 /* a player there touches a trigger kept clear of */
	int ( *section )( void *ctx, const float *point );               /* the merged map's section at point, -1: not merged */
} cof_bonus_kit_probe_t;

#define COF_BONUS_KIT_STEP 32.0f   /* a spot's floor this far at most above or below the start's: walked to */
#define COF_BONUS_KIT_SPREAD 32.0f /* apart from each other: each one +used on its own */
#define COF_BONUS_KIT_LIFT 8.0f    /* laid this far above its floor: a gun falls onto it (FallInit), an item drops */
#define COF_BONUS_KIT_EYE 36.0f    /* a standing player's origin over their feet (the player hull's mins) */

/* Rings round the start, nearest first; on each, straight ahead first, then out to both sides, behind last
 * (the start's forward and left parts of 0, 24, 48 ... 180 degrees). */
static const float cof_bonus_kit_radii[] = { 64.0f, 96.0f, 128.0f, 160.0f, 192.0f, 224.0f };
static const float cof_bonus_kit_dirs[][2] =
{
	{ 1.0f, 0.0f }, { 0.913545f, 0.406737f }, { 0.913545f, -0.406737f }, { 0.669131f, 0.743145f }, { 0.669131f, -0.743145f },
	{ 0.309017f, 0.951057f }, { 0.309017f, -0.951057f }, { -0.104528f, 0.994522f }, { -0.104528f, -0.994522f },
	{ -0.5f, 0.866025f }, { -0.5f, -0.866025f }, { -0.809017f, 0.587785f }, { -0.809017f, -0.587785f }, { -1.0f, 0.0f },
};
#define COF_BONUS_KIT_RINGS ( (int)( sizeof( cof_bonus_kit_radii ) / sizeof( cof_bonus_kit_radii[0] )))
#define COF_BONUS_KIT_DIRS ( (int)( sizeof( cof_bonus_kit_dirs ) / sizeof( cof_bonus_kit_dirs[0] )))
#define COF_BONUS_KIT_SPOTS ( COF_BONUS_KIT_RINGS * COF_BONUS_KIT_DIRS + 1 ) /* and the start's own floor, last */

/* One candidate: its feet (floor + 1) when a player could walk there from the start and stand there safely. start: the
 * spawn spot's origin (a standing player's centre); start_z: the start's floor; section: the start's; own: the start's
 * own spot (no line to check). */
static int CoF_BonusKitSpot( const cof_bonus_kit_probe_t *probe, const float *start, float start_z, int section, const float *point, int own, float *feet )
{
	float z;
	if( !own && !probe->reach( probe->ctx, start, point )) return 0;
	if( !probe->ground( probe->ctx, point, &z )) return 0;
	if( z > start_z + COF_BONUS_KIT_STEP || z < start_z - COF_BONUS_KIT_STEP ) return 0;
	feet[0] = point[0]; feet[1] = point[1]; feet[2] = z + 1.0f;
	if( !probe->stand( probe->ctx, feet ) || probe->danger( probe->ctx, feet )) return 0;
	return probe->section( probe->ctx, feet ) == section;
}

/* Spots for count items round the party's start (forward: the way it faces, a unit vector on the floor plane), each
 * reachable from the start in a straight line, on a floor near the start's, a player's room free of walls, water and
 * the triggers kept clear of, in the start's section: spread out while spots allow, then on spots nothing lies on,
 * then beside one another. out[i]: the item's origin (COF_BONUS_KIT_LIFT over its floor); placed[i]: 1 spread out,
 * 2 closer, 0 none (no safe spot at all). Returns how many have one. */
static int CoF_BonusKitPlace( const cof_bonus_kit_probe_t *probe, const float *start, const float *forward, int count, float ( *out )[3], int *placed )
{
	float spots[COF_BONUS_KIT_SPOTS][3], point[3], start_z;
	int good[COF_BONUS_KIT_SPOTS], used[COF_BONUS_KIT_SPOTS], chosen[64];
	int section, n = 0, i, j, k, pass, made = 0;
	if( !probe || !start || !forward || count <= 0 ) return 0;
	if( count > (int)( sizeof( chosen ) / sizeof( chosen[0] ))) count = (int)( sizeof( chosen ) / sizeof( chosen[0] ));
	for( i = 0; i < count; i++ ) placed[i] = 0;
	if( !probe->ground( probe->ctx, start, &start_z )) start_z = start[2] - COF_BONUS_KIT_EYE - 1.0f;
	section = probe->section( probe->ctx, start );
	for( i = 0; i < COF_BONUS_KIT_SPOTS; i++ )
	{
		int own = i == COF_BONUS_KIT_SPOTS - 1;
		point[0] = start[0]; point[1] = start[1]; point[2] = start[2];
		if( !own )
		{
			float r = cof_bonus_kit_radii[i / COF_BONUS_KIT_DIRS];
			float f = cof_bonus_kit_dirs[i % COF_BONUS_KIT_DIRS][0] * r, l = cof_bonus_kit_dirs[i % COF_BONUS_KIT_DIRS][1] * r;
			point[0] += f * forward[0] - l * forward[1];
			point[1] += f * forward[1] + l * forward[0];
		}
		good[i] = CoF_BonusKitSpot( probe, start, start_z, section, point, own, spots[i] );
		used[i] = 0;
		if( good[i] ) n++;
	}
	if( !n ) return 0;
	for( k = 0; k < count; k++ )
	{
		for( pass = 1; pass <= 3 && !placed[k]; pass++ )
			for( i = 0; i < COF_BONUS_KIT_SPOTS && !placed[k]; i++ )
			{
				int apart = 1;
				if( !good[i] || ( pass < 3 && used[i] )) continue;
				for( j = 0; pass == 1 && j < k && apart; j++ )
				{
					float dx, dy, dz;
					if( !placed[j] ) continue;
					dx = spots[chosen[j]][0] - spots[i][0];
					dy = spots[chosen[j]][1] - spots[i][1];
					dz = spots[chosen[j]][2] - spots[i][2];
					apart = dx * dx + dy * dy + dz * dz >= COF_BONUS_KIT_SPREAD * COF_BONUS_KIT_SPREAD;
				}
				if( !apart ) continue;
				chosen[k] = i;
				out[k][0] = spots[i][0]; out[k][1] = spots[i][1]; out[k][2] = spots[i][2] - 1.0f + COF_BONUS_KIT_LIFT;
				used[i]++;
				placed[k] = pass == 1 ? 1 : 2;
				made++;
			}
	}
	return made;
}

/* A player standing with their feet on feet, or walking up to it (COF_BONUS_KIT_CLEAR round their box), touches this
 * trigger's box (the engine pads a player's box by a unit for its touches). */
#define COF_BONUS_KIT_CLEAR 16.0f
static int CoF_BonusKitTouches( const float *feet, const float *absmin, const float *absmax )
{
	static const float lo[3] = { -16.0f - COF_BONUS_KIT_CLEAR, -16.0f - COF_BONUS_KIT_CLEAR, -1.0f };
	static const float hi[3] = { 16.0f + COF_BONUS_KIT_CLEAR, 16.0f + COF_BONUS_KIT_CLEAR, 2.0f * COF_BONUS_KIT_EYE + 1.0f };
	int k;
	for( k = 0; k < 3; k++ )
		if( feet[k] + lo[k] > absmax[k] || feet[k] + hi[k] < absmin[k] ) return 0;
	return 1;
}

/* The triggers the kit keeps clear of: a level exit, a hurt, a teleport or a push, and any trigger that fires a
 * scripted teleport (a cof_teleport: the "bounce" triggers that send whoever walks in back, sv_cof_party.c). */
static int CoF_BonusKitDanger( const char *classname, int fires_teleport )
{
	static const char *kinds[] = { "trigger_changelevel", "trigger_hurt", "trigger_teleport", "trigger_push" };
	int i;
	if( !classname || strncmp( classname, "trigger_", 8 )) return 0;
	if( fires_teleport ) return 1;
	for( i = 0; i < (int)( sizeof( kinds ) / sizeof( kinds[0] )); i++ )
		if( !strcmp( classname, kinds[i] )) return 1;
	return 0;
}
#endif
