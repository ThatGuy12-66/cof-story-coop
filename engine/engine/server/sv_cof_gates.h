/*
sv_cof_gates.h - Cry of Fear story co-op: small, verified changes to the retail hl.dll (sv_cof_gates.c)
GPL-3.0-or-later.

Each gate replaces a few bytes at an RVA of the hash-verified hl.dll (SV_CoF_StoryBase) while story co-op runs and
its cvar wants it, and only where the bytes are exactly the retail ones (or already ours); they go back when story
co-op ends. Research: docs/research/2026-10-18-cof-decompilation-survey.md (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_GATES_H
#define SV_COF_GATES_H

#if defined( _MSC_VER )
#define COF_GATES_FN static __inline
#else
#define COF_GATES_FN static inline
#endif

#define COF_GATE_MAX 24

typedef struct
{
	const char *name;
	unsigned int rva;
	int size;
	byte original[COF_GATE_MAX];
	byte changed[COF_GATE_MAX];
} cof_gate_t;

enum
{
	// CHalfLifeMultiplay::FPlayerCanTakeDamage (100C8700) refuses a player's damage only to a victim without
	// FL_FAKECLIENT: humans hurt the AI companions, and the companions each other. Without the jne after the flag
	// test, a player's damage is refused to any player (cof_coop_friendly_fire 0).
	COF_GATE_FRIENDLY_FIRE,
	// The single-player carry limits (cof_sp_carry). In multiplayer CBasePlayerItem::iMaxAmmo1 (101202F0) gives one
	// syringe (single player 10), three magazines of each gun, and -3 flares (WEAPON_NOCLIP * 3), so a flare comes
	// with none in reserve and every later one is refused; and each ammo box's AddAmmo (vtable +0x11C) has its own
	// multiplayer cap (glock 45, single player 150). The jump to the single-player branch after
	// g_pGameRules->IsMultiplayer() becomes unconditional.
	COF_GATE_CARRY_MAX_AMMO,
	COF_GATE_CARRY_AMMO_G43,
	COF_GATE_CARRY_AMMO_GLOCK,
	COF_GATE_CARRY_AMMO_M16,
	COF_GATE_CARRY_AMMO_MP5,
	COF_GATE_CARRY_AMMO_P345,
	COF_GATE_CARRY_AMMO_REVOLVER,
	COF_GATE_CARRY_AMMO_RIFLE,
	COF_GATE_CARRY_AMMO_SHELLS,
	COF_GATE_CARRY_AMMO_TMP,
	COF_GATE_CARRY_AMMO_VP70,
	// CRainModify::Use (100A99A0) returns at once in deathmatch ("Rain error: only static rain in multiplayer"): the
	// jump past that refusal becomes unconditional, and the named rain_modify changes player 1's rain as in single
	// player (cof_coop_weather; sv_cof_weather.c gives it to everyone).
	COF_GATE_RAIN_MODIFY,
	// CHalfLifeMultiplay::PlayerKilled (100C9160) hands DeathNotice g_pevLastInflictor (102266FC), which the game
	// clears after each TakeDamage; a player killed outside it by a monster (the chainsaw boss's grab calls Killed
	// itself) made DeathNotice read the NULL inflictor's classname (100C7E8D) and crash the game. PlayerKilled uses the
	// inflictor for nothing else: DeathNotice is handed the killer instead (push ebx). Always on: a crash fix.
	COF_GATE_DEATH_NOTICE,
	// The low-health limp (cof_sp_limp): below 25 health PreThink sets the physics key "low" (pm_shared: three
	// quarters speed, and three quarters again every other step, slower steps), only in single player, and Restore
	// puts it back after a level change. The jumps past the three SetPhysicsKeyValue calls go.
	COF_GATE_LIMP_ON,
	COF_GATE_LIMP_OFF,
	COF_GATE_LIMP_RESTORE,
	// The control hints (cof_sp_hints): CBasePlayer::ShowHint (100F3D90) returns at once in multiplayer, so co-op
	// never showed the tutorial (inventory, sprint, quick slots, saving, melee, ladders, dodge, objectives, the phone
	// light and dialling), the weapon hints or the "mash the buttons" prompt of a struggle. Each player still sees
	// each hint once (m_strHintsShown is the player's).
	COF_GATE_HINTS,
	// The morphine overdose (cof_sp_morphine_trip): a second syringe within sv_morphinetriptime of the last starts
	// MorphineTrip (CSyringe::Inject 1007AAB0), only in single player. The multiplayer test (1007AAC4..1007AAD6)
	// becomes "the syringe was the player's own": a partner's heal (pHealVictim, +0x1F8, multiplayer only) never
	// trips the healed one, who cannot see the timer. (The AI companions' own timer is kept at 0 by the engine.)
	COF_GATE_MORPHINE_TRIP,
	// Single-player details (cof_sp_details): a hard landing is a sound monsters hear (PostThink 100EEF6D); Spawn
	// cleared the message strip of the player spawning, not of everyone (UTIL_ProMessage's all, 100F45A8); the Glock
	// ejects its brass (10047292); the intro card follows the player's language (CCoFIntroduction::Use 100A4288);
	// in c_ending4b the first monster_booksimon is no co-op boss, whose stray Simon health bar outlived the fight
	// (CSickSimon::Spawn 10070AB6).
	COF_GATE_LANDING_SOUND,
	COF_GATE_SPAWN_STRIP,
	COF_GATE_GLOCK_BRASS,
	COF_GATE_INTRO_LANGUAGE,
	COF_GATE_BOOKSIMON_BOSS,
	COF_GATES
};

static const cof_gate_t cof_gates[COF_GATES] =
{
	{ "friendly-fire", 0xC8722, 2, { 0x75, 0x06 }, { 0x90, 0x90 } },
	{ "carry-max-ammo", 0x120304, 2, { 0x74, 0x76 }, { 0xEB, 0x76 } },
	{ "carry-ammo-g43", 0x43467, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-glock", 0x45657, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-m16", 0x50BB7, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-mp5", 0x5A397, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-p345", 0x5D927, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-revolver", 0x63B57, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-rifle", 0x65E87, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-shells", 0x6CB36, 2, { 0x74, 0x1F }, { 0xEB, 0x1F } },
	{ "carry-ammo-tmp", 0x7E0C7, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "carry-ammo-vp70", 0x836D7, 2, { 0x74, 0x53 }, { 0xEB, 0x53 } },
	{ "rain-modify", 0xA99C7, 2, { 0x7B, 0x14 }, { 0xEB, 0x14 } },
	{ "death-notice", 0xC916B, 3, { 0xFF, 0x75, 0x10 }, { 0x53, 0x90, 0x90 } },
	{ "limp-on", 0xEF9D8, 2, { 0x75, 0x1C }, { 0x90, 0x90 } },
	{ "limp-off", 0xEFB23, 2, { 0x75, 0x1C }, { 0x90, 0x90 } },
	{ "limp-restore", 0xF16CC, 2, { 0x75, 0x2D }, { 0x90, 0x90 } },
	{ "hints", 0xF3DC7, 6, { 0x0F, 0x85, 0x45, 0x02, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
	// mov ecx, [g_pGameRules]; test ecx, ecx; je; mov eax, [ecx]; call [eax+0x20]; test eax, eax; jne 1007AAF9
	// -> cmp dword ptr [esi+0x1F8], 0; jne 1007AAF9; nops
	{ "morphine-trip", 0x7AAC4, 19, { 0x8B, 0x0D, 0xE4, 0x2E, 0x22, 0x10, 0x85, 0xC9, 0x74, 0x2B, 0x8B, 0x01, 0xFF, 0x50, 0x20, 0x85, 0xC0, 0x75, 0x22 },
		{ 0x83, 0xBE, 0xF8, 0x01, 0x00, 0x00, 0x00, 0x75, 0x2C, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
	{ "landing-sound", 0xEEF6D, 2, { 0x75, 0x22 }, { 0x90, 0x90 } },
	{ "spawn-strip", 0xF45A8, 2, { 0x6A, 0x01 }, { 0x6A, 0x00 } },
	{ "glock-brass", 0x47292, 6, { 0x0F, 0x85, 0x9E, 0x01, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
	{ "intro-language", 0xA4288, 2, { 0x74, 0x14 }, { 0xEB, 0x14 } },
	{ "booksimon-boss", 0x70AB6, 6, { 0x0F, 0x84, 0x47, 0x01, 0x00, 0x00 }, { 0xE9, 0x48, 0x01, 0x00, 0x00, 0x90 } },
};

// Brings one gate to the wanted state through write (which changes executable bytes); 1 when it is in that state now,
// 0 when the bytes are neither the retail ones nor ours (another DLL build: left alone).
COF_GATES_FN int CoF_GateSet( byte *base, const cof_gate_t *g, int on, int ( *write )( byte *p, const byte *value, int size ))
{
	byte *p = base + g->rva;
	const byte *want = on ? g->changed : g->original, *other = on ? g->original : g->changed;
	if( !memcmp( p, want, g->size )) return 1;
	if( memcmp( p, other, g->size )) return 0;
	return write( p, want, g->size );
}

#endif // SV_COF_GATES_H
