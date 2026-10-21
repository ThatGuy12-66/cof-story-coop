/* Cry of Fear story co-op: what a chapter start hands out (1.1.8). GPL-3.0-or-later.
 * The app's chapter list starts a chapter with `map`: every player spawns empty-handed (no story map has a
 * game_player_equip) and the story values earlier chapters set are unset. The app names the chapter's map in
 * cof_chapter_start (never for New game or Continue) and the server hands out, once, what the story player carries
 * at that chapter's title. Research: the chapter-starts audit of 2026-10-21. Pure: the offline harness drives it. */
#ifndef SV_COF_CHAPTER_KIT_H
#define SV_COF_CHAPTER_KIT_H
#include <string.h>

#define COF_CHAPTER_WEAPONS 4

typedef struct
{
	const char *map;
	const char *weapons[COF_CHAPTER_WEAPONS]; // given to every player, NULL-padded
	int doctor_weapon; // m_iDoctorWeapon: 2 the revolver (the c_attic doctor never fights while it is 0)
	int three_slots; // the bag the story takes in c_bridge (Chapter 6)
} cof_chapter_kit_t;

static const cof_chapter_kit_t *CoF_ChapterKit( const char *map )
{
	static const cof_chapter_kit_t kits[] =
	{
		// Chapter 2: the phone dials the c_basement door code; the glock is c_apartment1's gift.
		{ "c_basement", { "weapon_mobile", "weapon_switchblade", "weapon_glock", NULL }, 0, 0 },
		// Chapters 3 to 5: and the c_sewer2 shotgun.
		{ "c_sewer3", { "weapon_mobile", "weapon_switchblade", "weapon_glock", "weapon_shotgun" }, 0, 0 },
		{ "c_park3", { "weapon_mobile", "weapon_switchblade", "weapon_glock", "weapon_shotgun" }, 0, 0 },
		{ "c_subwaysick2", { "weapon_mobile", "weapon_switchblade", "weapon_glock", "weapon_shotgun" }, 0, 0 },
		// Chapter 6 (c_bridge) starts with the story's empty bag: nothing.
		// Chapter 7: the doctor only fights once c_asylum1 (Chapter 6) chose his weapon.
		{ "c_attic", { "weapon_shotgun", "weapon_p345", NULL, NULL }, 2, 1 },
		// Chapter 8 and the endings: c_endingNc lets the party on only after four kills, with no gun on the way.
		{ "c_ending1b", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		{ "c_ending2b", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		{ "c_ending3b", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		{ "c_ending1", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		{ "c_ending2", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		{ "c_ending3", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
		// Ending 5's kill counters (c_ending5c) want a gun for everyone; Ending 4's wheelchair arms its rider.
		{ "c_ending5", { "weapon_shotgun", "weapon_m16", NULL, NULL }, 0, 1 },
	};
	size_t i;
	if( !map || !map[0] ) return NULL;
	for( i = 0; i < sizeof( kits ) / sizeof( kits[0] ); i++ )
		if( !strcmp( kits[i].map, map )) return &kits[i];
	return NULL;
}
#endif
