/*
cof_abi.h - Cry of Fear's engine structures that are larger than Xash3D's
GPL-3.0-or-later.

Cry of Fear's own GoldSrc build (hw.dll of 2013-04-21) appends one int to two structures the game DLLs share with the
engine (docs/research/2026-10-18-cof-decompilation-survey.md 1.2, the launcher repo):
- usercmd_t + 0x34 `is_donator` (0x38 bytes). client.dll's CL_CreateMove writes the local Steam user's donator level
  there (client 1007ACAF); hl.dll's CmdStart copies it into the player's CBaseEntity + 0xC4 (hl 1001DAF9), where
  multiplayer IsDonator() (not 0, 3 or 4: weapon_tmp pickups, ammo_tmp kept), IsDeveloper() (4: debug commands) and
  the "mappers only" MP5 (2-4) read it.
- clientdata_t + 0x1DC `isdonator` (0x1E0 bytes): hl.dll's UpdateClientData writes the player's + 0xC4 back.
Xash's structures are 52 and 0x1DC bytes: without these wrappers the game reads the next command's first bytes (or
stack garbage for an AI companion), client.dll writes into runcmd_t.processedfuncs, and the server's client data
write lands in the frame's first weapon record.

Plain C: the offline tests share it.
*/
#ifndef COF_ABI_H
#define COF_ABI_H

#if defined( _MSC_VER )
#define COF_ABI_FN static __inline
#else
#define COF_ABI_FN static inline
#endif

typedef struct cof_usercmd_s
{
	usercmd_t cmd;
	int is_donator;
} cof_usercmd_t;

typedef struct cof_clientdata_s
{
	clientdata_t cd;
	int isdonator;
} cof_clientdata_t;

STATIC_ASSERT( sizeof( cof_usercmd_t ) == 0x38, "Cry of Fear's usercmd_t is 0x38 bytes" );
STATIC_ASSERT( sizeof( cof_clientdata_t ) == 0x1E0, "Cry of Fear's clientdata_t is 0x1E0 bytes" );

// The donator level every command carries to the game (cvar cof_donator): 0 is retail's non-donor (multiplayer
// refuses weapon_tmp pickups and turns ammo_tmp into Glock ammo), anything else a donator. Never the mapper or
// developer levels (2-4), which open the game's debug commands.
COF_ABI_FN int CoF_DonatorLevel( float cvar )
{
	return cvar != 0.0f ? 1 : 0;
}

COF_ABI_FN void CoF_DonatorCommand( cof_usercmd_t *out, const usercmd_t *in, int level )
{
	out->cmd = *in;
	out->is_donator = level;
}

#endif // COF_ABI_H
