/*
sv_cof_inventory12.c - Cry of Fear story co-op: the 12-slot inventory, server side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md section 1.

The disk game DLL stays stock. After the story gate's file hash check the keep
group (sv_cof_inventory12_patch.h) is applied whenever story co-op runs, and the
wide group only on a map that runs at 12 slots: the cvar asks for it, the bytes
are the expected ones, and every person connected at map load - the listen
host's own client too - has the 12-slot screen. A person who joins a map at 12
needs it as well, or is turned away: a retail client.dll crashes on slot 7.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_cheats.h"
#include "sv_cof_inventory12.h"
#include "sv_cof_inventory12_patch.h"
#if XASH_WIN32
#include <windows.h>
#endif

static struct
{
	convar_t *cvar;            // cof_inventory_slots
	unsigned char *base;       // the hl.dll image the bytes were written in, NULL for none
	void *instance;            // the svgame.hInstance of that image
	int slots;                 // this map: 12 or 6
	char reason[48];           // why this map runs at six, "" at 12
	struct { int map, userid, slots; } told[MAX_CLIENTS]; // what each client slot was last told, on which map, as whom
} cof_inv12;

#ifndef COF_INV12_WRITE
static int CoF_Inv12WriteCode( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	DWORD protect, ignored;
	if( memcmp( p, expect, len ) || !VirtualProtect( p, len, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, len );
	FlushInstructionCache( GetCurrentProcess(), p, len );
	VirtualProtect( p, len, protect, &ignored );
	return 1;
#else
	return 0;
#endif
}
#define COF_INV12_WRITE CoF_Inv12WriteCode
#endif

#define COF_INV12_KEEP cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep )
#define COF_INV12_WIDE cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide )

int SV_CoF_InventorySlots( void )
{
	return cof_inv12.base && cof_inv12.slots == 12 ? 12 : 6;
}

static qboolean CoF_Inv12Person( const sv_client_t *cl )
{
	return cl->state >= cs_connected && !FBitSet( cl->flags, FCL_FAKECLIENT );
}

static void CoF_Inv12Tell( int i, int slots )
{
	sv_client_t *cl = &svs.clients[i];
	MSG_BeginServerCmd( &cl->netchan.message, svc_stufftext );
	MSG_WriteString( &cl->netchan.message, va( "cof_inv_slots %d\n", slots ));
	cof_inv12.told[i].map = svs.spawncount;
	cof_inv12.told[i].userid = cl->userid;
	cof_inv12.told[i].slots = slots;
}

void SV_CoF_Inventory12Close( void )
{
	int i;
	// story co-op switched off mid-map: nobody may stay on the grid
	if( SV_CoF_InventorySlots( ) == 12 && svs.clients && sv.state == ss_active )
	{
		for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
			if( CoF_Inv12Person( &svs.clients[i] )) CoF_Inv12Tell( i, 6 );
	}
	if( cof_inv12.base && cof_inv12.instance == svgame.hInstance )
	{
		CoF_Inv12GroupSet( cof_inv12.base, COF_INV12_WIDE, 0, COF_INV12_WRITE );
		CoF_Inv12GroupSet( cof_inv12.base, COF_INV12_KEEP, 0, COF_INV12_WRITE );
	}
	cof_inv12.base = NULL;
	cof_inv12.instance = NULL;
	cof_inv12.slots = 6;
}

static qboolean CoF_Inv12HasScreen( const char *userinfo )
{
	return !Q_strcmp( Info_ValueForKey( userinfo, COF_INV12_USERINFO ), "12" );
}

static void CoF_Inv12Six( const char *reason )
{
	Q_strncpy( cof_inv12.reason, reason, sizeof( cof_inv12.reason ));
	cof_inv12.slots = 6;
	Con_Printf( "cofstory phase=inventory12-off reason=%s\n", reason );
}

void SV_CoF_Inventory12Activate( void )
{
	unsigned char *base;
	const char *reason = NULL;
	int i;
	cof_inv12.slots = 6;
	cof_inv12.reason[0] = 0;
	if( !SV_CoF_StoryCoopEnabled( )) return; // closing the gate already put the stock bytes back
	base = (unsigned char *)SV_CoF_StoryBase( );
	if( cof_inv12.base && ( cof_inv12.base != base || cof_inv12.instance != svgame.hInstance ))
		cof_inv12.base = NULL; // another image: the old one is gone with its DLL
	if( !base )
	{
		CoF_Inv12Six( "platform" );
		return;
	}
	// keep at six slots too, or the retail clears wipe slots 8..13
	if( !CoF_Inv12GroupSet( base, COF_INV12_KEEP, 1, COF_INV12_WRITE ))
	{
		cof_inv12.base = NULL;
		CoF_Inv12Six( "bytes" );
		return;
	}
	cof_inv12.base = base;
	cof_inv12.instance = svgame.hInstance;
	if( !cof_inv12.cvar || (int)cof_inv12.cvar->value != 12 ) reason = "cvar";
	else if( !Host_IsDedicated( ) && !CL_CoF_Inventory12Ready( )) reason = "host-client";
	for( i = 0; !reason && i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( CoF_Inv12Person( &svs.clients[i] ) && !CoF_Inv12HasScreen( svs.clients[i].userinfo ))
			reason = va( "client:%d", svs.clients[i].userid );
	if( reason )
	{
		CoF_Inv12GroupSet( base, COF_INV12_WIDE, 0, COF_INV12_WRITE );
		CoF_Inv12Six( reason );
		return;
	}
	if( !CoF_Inv12GroupSet( base, COF_INV12_WIDE, 1, COF_INV12_WRITE ))
	{
		CoF_Inv12Six( "bytes" );
		return;
	}
	cof_inv12.slots = 12;
	Con_Printf( "cofstory phase=inventory12 slots=12\n" );
}

void SV_CoF_Inventory12Frame( void )
{
	int i, slots;
	if( sv.state != ss_active || !SV_CoF_StoryCoopEnabled( )) return;
	slots = SV_CoF_InventorySlots( );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		// Told once spawned, never while connecting: the listen host's own
		// client gets the stufftext before svc_serverdata, whose CL_ClearState
		// forgets it. Anyone below spawned (a reconnect into the same slot
		// too) is told again once in the game.
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ))
		{
			cof_inv12.told[i].map = 0;
			continue;
		}
		if( cof_inv12.told[i].map == svs.spawncount && cof_inv12.told[i].userid == cl->userid && cof_inv12.told[i].slots == slots ) continue;
		CoF_Inv12Tell( i, slots );
	}
}

qboolean SV_CoF_Inventory12Refuse( const char *userinfo, qboolean local )
{
	if( local || sv.state != ss_active || SV_CoF_InventorySlots( ) != 12 || CoF_Inv12HasScreen( userinfo )) return false;
	Con_Printf( "cofstory phase=inventory12-join-refused name=%s\n", Info_ValueForKey( userinfo, "name" ));
	return true;
}

// A slot number argument: one or two digits, 1..limit (longer ones are
// refused, so the game's own atoi cannot read a different number than ours).
static qboolean CoF_Inv12SlotArg( int index, int limit )
{
	const char *s = Cmd_Argv( index );
	int i;
	if( !s[0] ) return false;
	for( i = 0; s[i]; i++ )
		if( i >= 2 || s[i] < '0' || s[i] > '9' ) return false;
	i = Q_atoi( s );
	return i >= 1 && i <= limit;
}

qboolean SV_CoF_Inventory12ClientCommand( sv_client_t *cl )
{
	const char *cmd = Cmd_Argv( 0 );
	qboolean one, two;
	int limit;
	one = !Q_stricmp( cmd, "inventoryuse" ) || !Q_stricmp( cmd, "inventoryequip" ) || !Q_stricmp( cmd, "inventorydrop" ) ||
		!Q_stricmp( cmd, "quickselset1" ) || !Q_stricmp( cmd, "quickselset2" ) || !Q_stricmp( cmd, "quickselset3" );
	two = !Q_stricmp( cmd, "inventorycombine" ) || !Q_stricmp( cmd, "inventorydualwield" );
	if(( !one && !two ) || !SV_CoF_StoryCoopEnabled( )) return false;
	limit = SV_CoF_RetailInThreeSlots( cl ) ? 3 : SV_CoF_InventorySlots( );
	if( CoF_Inv12SlotArg( 1, limit ) && ( one || CoF_Inv12SlotArg( 2, limit ))) return false;
	Con_Printf( "cofstory phase=inventory-cmd-refused userid=%d cmd=%s args=\"%s %s\"\n", cl->userid, cmd, Cmd_Argv( 1 ), Cmd_Argv( 2 ));
	return true;
}

static void CoF_Inv12Status_f( void )
{
	int i;
	Con_Printf( "cofstory phase=inventory12-status slots=%d cvar=%d keep=%d wide=%d reason=%s\n", SV_CoF_InventorySlots( ),
		cof_inv12.cvar ? (int)cof_inv12.cvar->value : 0,
		cof_inv12.base ? CoF_Inv12GroupState( cof_inv12.base, COF_INV12_KEEP ) : 0,
		cof_inv12.base ? CoF_Inv12GroupState( cof_inv12.base, COF_INV12_WIDE ) : 0,
		cof_inv12.reason[0] ? cof_inv12.reason : "none" );
	if( !svs.clients || sv.state != ss_active ) return;
	// The live check of the spec's residual risk: the player fields right after
	// the slot array (server-inventory-re.md) must survive a full clear, and
	// nothing may write +0x2230..+0x226B, the room after slot 12.
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		const byte *pv;
		float f2270, trip_time, tripping_time;
		uint32_t f2280, f2284, dword;
		int at, dead = 0;
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !( pv = SV_CoF_StoryEntityPrivate( cl->edict, 0x2288 ))) continue;
		for( at = 0x2230; at < 0x226C; at += 4 )
		{
			memcpy( &dword, pv + at, sizeof( dword ));
			if( dword ) dead++;
		}
		memcpy( &f2270, pv + 0x2270, sizeof( f2270 ));
		memcpy( &trip_time, pv + 0x2274, sizeof( trip_time ));         // m_fMorphineTripTime
		memcpy( &tripping_time, pv + 0x227C, sizeof( tripping_time )); // m_fTrippingTime
		memcpy( &f2280, pv + 0x2280, sizeof( f2280 ));
		memcpy( &f2284, pv + 0x2284, sizeof( f2284 ));
		Con_Printf( "cofstory phase=inventory12-fields userid=%d first_syringe=%d f2270=%g trip_time=%g tripping=%d tripping_time=%g f2280=%08x f2284=%08x dead=%d\n",
			cl->userid, pv[0x226C] /* m_bUsedFirstSyringe */, (double)f2270, (double)trip_time, pv[0x2278] /* m_bIsTripping */, (double)tripping_time,
			(unsigned int)f2280, (unsigned int)f2284, dead );
	}
}

void SV_CoF_Inventory12Init( void )
{
	cof_inv12.cvar = Cvar_Get( "cof_inventory_slots", "12", 0, "story co-op inventory slots: 12, or 6 for the retail inventory (from the next map)" );
	cof_inv12.slots = 6;
	Cmd_AddCommand( "cof_inventory12_status", CoF_Inv12Status_f, "story co-op: the 12-slot inventory's state" );
}
