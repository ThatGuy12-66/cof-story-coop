/*
sv_cof_tools.h - Cry of Fear story co-op: the player tools' rules (sv_cof_tools.c)
GPL-3.0-or-later.

The player tools (AGENT_NOTES.md "Player tools (2026-11-09)", the launcher repo): unstuck, a text ping, dropping one box
of spare ammo, a scare, sprays, teammate outlines when far, and the host's revive and auto-respawn times. This project's
own behaviour, written for this engine; nothing here is taken from another co-op patch. Weapon ids are m_iId (hl.dll
GetItemInfo, as sv_cof_weaponmodel.h); ammo boxes and the m_rgAmmo table: docs/research/
2026-10-12-companion-weapons-items-re.md sections 1.1 and 1.5 (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_TOOLS_H
#define SV_COF_TOOLS_H

#include <stddef.h>
#include <string.h>
#include <math.h>

#if defined( _MSC_VER )
#define COF_TOOLS_FN static __inline
#else
#define COF_TOOLS_FN static inline
#endif

// The host's settings: a value in range is kept, anything else (out of range, not a number) is the default.
#define COF_TOOLS_CPR_MIN         1.0f  // sv_coopcprtime: seconds of CPR to revive a downed partner
#define COF_TOOLS_CPR_MAX         20.0f
#define COF_TOOLS_CPR_DEFAULT     5.0f
#define COF_TOOLS_RESPAWN_MIN     5.0f  // cof_story_respawn_delay: seconds (this project's delay, not retail 384)
#define COF_TOOLS_RESPAWN_MAX     90.0f
#define COF_TOOLS_RESPAWN_DEFAULT 10.0f
#define COF_TOOLS_OUTLINES_OFF     0    // cof_outlines: teammates outlined when far, off by default
#define COF_TOOLS_OUTLINES_ON      1
#define COF_TOOLS_OUTLINES_DEFAULT COF_TOOLS_OUTLINES_OFF

COF_TOOLS_FN float CoF_ToolsClampCprTime( float value )
{
	return value >= COF_TOOLS_CPR_MIN && value <= COF_TOOLS_CPR_MAX ? value : COF_TOOLS_CPR_DEFAULT;
}

COF_TOOLS_FN float CoF_ToolsClampRespawnDelay( float value )
{
	return value >= COF_TOOLS_RESPAWN_MIN && value <= COF_TOOLS_RESPAWN_MAX ? value : COF_TOOLS_RESPAWN_DEFAULT;
}

COF_TOOLS_FN int CoF_ToolsClampOutlines( int value )
{
	return value == COF_TOOLS_OUTLINES_OFF || value == COF_TOOLS_OUTLINES_ON ? value : COF_TOOLS_OUTLINES_DEFAULT;
}

// Seconds before one player can use the same tool again.
#define COF_TOOLS_COOLDOWN_UNSTUCK 10.0
#define COF_TOOLS_COOLDOWN_PING    2.0
#define COF_TOOLS_COOLDOWN_SCARE   20.0
#define COF_TOOLS_COOLDOWN_DROP    1.0

// A tool is ready again once now reaches next (the last use plus its cooldown).
COF_TOOLS_FN int CoF_ToolsReady( double now, double next )
{
	return now >= next;
}

// Unstuck is refused while the player is down, in a scene, on a ladder, or while a camera holds them.
COF_TOOLS_FN int CoF_ToolsUnstuckRefused( int down, int in_scene, int on_ladder, int camera_holds )
{
	return down || in_scene || on_ladder || camera_holds;
}

// What unstuck does: search a clear spot only when the hull is stuck; a free player with a partner is moved beside
// them (SV_CoF_PartyMoveBeside); a free player alone is told they are not stuck.
#define COF_TOOLS_UNSTUCK_FREE   0
#define COF_TOOLS_UNSTUCK_SEARCH 1
#define COF_TOOLS_UNSTUCK_BESIDE 2

COF_TOOLS_FN int CoF_ToolsUnstuckPlan( int hull_stuck, int has_partner )
{
	if( hull_stuck ) return COF_TOOLS_UNSTUCK_SEARCH;
	return has_partner ? COF_TOOLS_UNSTUCK_BESIDE : COF_TOOLS_UNSTUCK_FREE;
}

// Where a player looks, from v_angle in GoldSrc degrees (pitch v_angle[0], positive looks down; yaw v_angle[1]):
// ( cp*cy, cp*sy, -sp ), the forward vector of AngleVectors, which the server cannot call (not declared there).
#define COF_TOOLS_DEG2RAD ( 3.14159265358979323846 / 180.0 )

COF_TOOLS_FN void CoF_ToolsForward( float pitch, float yaw, float out[3] )
{
	double p = (double)pitch * COF_TOOLS_DEG2RAD, y = (double)yaw * COF_TOOLS_DEG2RAD;
	double cp = cos( p ), sp = sin( p ), cy = cos( y ), sy = sin( y );
	out[0] = (float)( cp * cy );
	out[1] = (float)( cp * sy );
	out[2] = (float)( -sp );
}

// Player fields the tools touch (hl.dll CBasePlayer).
#define COF_TOOLS_AMMO_OFFSET       0x2540 // int m_rgAmmo[32]: slot i at +0x2540 + 4*i (the weapon's m_iPrimaryAmmoType)
#define COF_TOOLS_AMMO_SLOTS        32
#define COF_TOOLS_OBJECTIVE_OFFSET  0x1E8C // write COF_TOOLS_OBJECTIVE_RESEND here and the game resends the objective
#define COF_TOOLS_OBJECTIVE_RESEND  0xFF
#define COF_TOOLS_BLACKWHITE_OFFSET 0x1EF0 // BlackWhite (noir), 1 byte; clearing it needs an explicit message
#define COF_TOOLS_BLACKWHITE_SIZE   1
#define COF_TOOLS_RENDERFX_GLOWSHELL 19    // kRenderFxGlowShell (teammate outlines; may not draw in CoF's renderer)

// The byte offset of m_rgAmmo[slot] in the player, or -1 outside the array.
COF_TOOLS_FN int CoF_ToolsAmmoSlotOffset( int slot )
{
	return slot >= 0 && slot < COF_TOOLS_AMMO_SLOTS ? COF_TOOLS_AMMO_OFFSET + 4 * slot : -1;
}

// Dropping one box of spare ammo (SV_CreateNamedEntity of the box, no bonus/copy flag) for the gun in hand.
#define COF_TOOLS_WEAPON_DUAL 15 // weapon_dualweapon (COF_WEAPONMODEL_DUAL): two guns, no one box; refused
#define COF_TOOLS_WEAPON_MP5  27 // weapon_mp5: its ammo index is not confirmed, so its ammo is never dropped

// The guns with an ammo_* box in the story maps, and what one box gives (single-player amount; the game caps
// multiplayer at three magazines itself). Everything else (melee, lights, syringe, flare, camera, FAMAS, Browning,
// dual, MP5) has no box here and is refused.
typedef struct { int id; const char *entity; int box; } cof_tools_ammo_t;
static const cof_tools_ammo_t cof_tools_ammo[] =
{
	{ 3, "ammo_glock", 15 },    // weapon_glock (=weapon_beretta)
	{ 7, "ammo_p345", 8 },      // weapon_p345 (=weapon_P228)
	{ 21, "ammo_revolver", 5 }, // weapon_revolver
	{ 4, "ammo_shells", 9 },    // weapon_shotgun (buckshot)
	{ 11, "ammo_rifle", 5 },    // weapon_rifle
	{ 9, "ammo_m16", 20 },      // weapon_m16 (=weapon_mp5k)
	{ 19, "ammo_vp70", 18 },    // weapon_vp70
	{ 14, "ammo_tmp", 30 },     // weapon_tmp (=weapon_uzi)
};

// The ammo box classname for this weapon id, or NULL when none is dropped.
COF_TOOLS_FN const char *CoF_ToolsAmmoEntity( int weapon_id )
{
	size_t i;
	if( weapon_id == COF_TOOLS_WEAPON_DUAL || weapon_id == COF_TOOLS_WEAPON_MP5 ) return NULL;
	for( i = 0; i < sizeof( cof_tools_ammo ) / sizeof( cof_tools_ammo[0] ); i++ )
		if( cof_tools_ammo[i].id == weapon_id ) return cof_tools_ammo[i].entity;
	return NULL;
}

// What one box of that weapon's ammo gives, or 0 when none is dropped.
COF_TOOLS_FN int CoF_ToolsAmmoBox( int weapon_id )
{
	size_t i;
	if( !CoF_ToolsAmmoEntity( weapon_id )) return 0;
	for( i = 0; i < sizeof( cof_tools_ammo ) / sizeof( cof_tools_ammo[0] ); i++ )
		if( cof_tools_ammo[i].id == weapon_id ) return cof_tools_ammo[i].box;
	return 0;
}

// Dropping ammo is refused for the dual weapon, the MP5 and anything without a box.
COF_TOOLS_FN int CoF_ToolsDropRefused( int weapon_id )
{
	return CoF_ToolsAmmoEntity( weapon_id ) == NULL;
}

// Sprays: a .png or .bmp file (any case) with a name before the extension; nothing else is copied to logos/.
COF_TOOLS_FN int CoF_ToolsSprayExtension( const char *ext, const char *want )
{
	for( ; *ext && *want; ext++, want++ )
	{
		char c = ( *ext >= 'A' && *ext <= 'Z' ) ? (char)( *ext + 32 ) : *ext;
		if( c != *want ) return 0;
	}
	return !*ext && !*want;
}

COF_TOOLS_FN int CoF_ToolsSprayAllowed( const char *path )
{
	const char *dot, *slash, *back;
	if( !path ) return 0;
	dot = strrchr( path, '.' );
	slash = strrchr( path, '/' );
	back = strrchr( path, '\\' );
	if( back && ( !slash || back > slash )) slash = back;
	if( !dot || dot == path || ( slash && dot <= slash + 1 )) return 0;
	return CoF_ToolsSprayExtension( dot + 1, "png" ) || CoF_ToolsSprayExtension( dot + 1, "bmp" );
}

#endif // SV_COF_TOOLS_H
