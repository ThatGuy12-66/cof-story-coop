/*
sv_cof_globals.h - Cry of Fear story co-op: hl.dll's single globals that hold one player's state (sv_cof_globals.c)
GPL-3.0-or-later.

Written for one player, these are file globals in the retail hl.dll, so in co-op every player's think overwrote them
(docs/research/2026-10-18-cof-decompilation-survey.md section 4, the launcher repo):
- m_bMorphineMuffle: PreThink copies the player's m_bIsTripping (100EF7C7) and every server sound is played at 25 %
  while it is set (EMIT_SOUND_DYN), so one player's morphine trip muffled the world for everyone;
- m_gbIsInCutscene: PostThink (100EDF36) hides the player model and keeps heartbeat and low-health sounds quiet;
- CalculateWeaponAccuracy's edge flags and floor (1011BB50, run in each PreThink): a moving player next to a standing
  one got the one-time kick every frame, up to the 10 degree cap;
- gDualWeapon (0x102118C0, CDualWeaponRight): one object ran every player's left hand, so with two players dual
  wielding the wrong one swung, fired or toggled the phone light (CoF_Dual_Weapon.cpp:162-241); its own fields
  (+0xDC..+0x134) are swapped, not its vtable or pev;
- lastquickslotpressed (QuickSelectItem 100F07F0): two quick-slot keys within a quarter second dual-wield their
  items, and one player's key paired up with another's (the time it counts from is the player's own, +0x5D8).
Each player keeps a copy; while a player's command runs the DLL holds theirs, otherwise the host's.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_GLOBALS_H
#define SV_COF_GLOBALS_H

#if defined( _MSC_VER )
#define COF_GLOBALS_FN static __inline
#else
#define COF_GLOBALS_FN static inline
#endif

typedef struct
{
	const char *name;
	unsigned int rva;
	int size;
} cof_global_t;

#define COF_GLOBALS 9
#define COF_GLOBALS_BYTES 102 // the sizes added up

static const cof_global_t cof_player_globals[COF_GLOBALS] =
{
	{ "morphine-muffle", 0x226489, 1 }, // bool m_bMorphineMuffle
	{ "cutscene", 0x226477, 1 },        // bool m_gbIsInCutscene
	{ "spread-moving", 0x231F0A, 1 },   // bool wasmoving
	{ "spread-jumping", 0x231F0B, 1 },  // bool wasjumping
	{ "spread-ducking", 0x231F0E, 1 },  // bool wasducking
	{ "spread-sprinting", 0x231F0F, 1 },// bool wassprinting
	{ "spread-minimum", 0x231F5C, 4 },  // int minimum
	{ "dual-weapon", 0x21199C, 0x58 },  // gDualWeapon.m_iCurrentShot .. m_iIdleAnimation2
	{ "quick-slot", 0x226718, 4 },      // int lastquickslotpressed
};

// A player's copy into the DLL's globals.
COF_GLOBALS_FN void CoF_GlobalsLoad( unsigned char *base, const unsigned char *slot )
{
	int i, off = 0;
	for( i = 0; i < COF_GLOBALS; off += cof_player_globals[i].size, i++ )
		memcpy( base + cof_player_globals[i].rva, slot + off, cof_player_globals[i].size );
}

// The DLL's globals into a player's copy.
COF_GLOBALS_FN void CoF_GlobalsSave( const unsigned char *base, unsigned char *slot )
{
	int i, off = 0;
	for( i = 0; i < COF_GLOBALS; off += cof_player_globals[i].size, i++ )
		memcpy( slot + off, base + cof_player_globals[i].rva, cof_player_globals[i].size );
}

// The game's CVAR_GET/SET_FLOAT( "room_type" ) sets the DSP of the machine the server runs on. Whose it is: the slot of
// the player whose command runs (the engine keeps it and tells that player), or -1: the host's own cvar (also when no
// command runs, as the game always did). Without the cvar (-nosound) the host's is kept too.
COF_GLOBALS_FN int CoF_RoomTypeOwner( const char *name, int slot, int host, int cvar_exists )
{
	if( !name || strcmp( name, "room_type" ) || slot < 0 ) return -1;
	return host && cvar_exists ? -1 : slot;
}

#endif // SV_COF_GLOBALS_H
