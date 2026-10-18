/*
sv_cof_globals.c - Cry of Fear story co-op: hl.dll's single globals that hold one player's state (sv_cof_globals.h)
GPL-3.0-or-later.

With cof_coop_player_state (default 1), around every spawned player's command (SV_RunCmd, the AI companions too) the
DLL holds that player's copy of the globals, otherwise the host's; and while a guest's or a companion's command runs,
the game's room_type is that player's: kept by the engine and sent to that guest (svc_roomtype) when it changes,
instead of the host's own DSP.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_globals.h"

static CVAR_DEFINE_AUTO( cof_coop_player_state, "1", 0, "Cry of Fear story co-op: the game's one-player globals (morphine muffle, cutscene flag, weapon spread, room_type) kept per player; 0: shared, as the game does" );

static byte cof_globals_slots[MAX_CLIENTS][COF_GLOBALS_BYTES];
static float cof_globals_room_type[MAX_CLIENTS];
static sv_client_t *cof_globals_client; // whose command runs, with their copy in the DLL
static byte *cof_globals_base;

void SV_CoF_GlobalsInit( void )
{
	Cvar_RegisterVariable( &cof_coop_player_state );
}

static int CoF_GlobalsSlot( const sv_client_t *cl )
{
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	return slot >= 0 && slot < MAX_CLIENTS && slot < svs.maxclients ? slot : -1;
}

void SV_CoF_GlobalsEnter( sv_client_t *cl )
{
	byte *base;
	int slot = CoF_GlobalsSlot( cl );
	cof_globals_client = NULL;
	if( slot < 0 || cl->state != cs_spawned || cof_coop_player_state.value == 0.0f || !SV_CoF_StoryCoopEnabled( )) return;
	if( !( base = (byte *)SV_CoF_StoryBase( ))) return;
	if( base != cof_globals_base ) // another DLL instance: nobody's copy is in it
	{
		memset( cof_globals_slots, 0, sizeof( cof_globals_slots ));
		cof_globals_base = base;
		CoF_GlobalsSave( base, cof_globals_slots[0] );
	}
	CoF_GlobalsLoad( base, cof_globals_slots[slot] );
	cof_globals_client = cl;
}

void SV_CoF_GlobalsLeave( sv_client_t *cl )
{
	int slot = CoF_GlobalsSlot( cl );
	if( !cof_globals_client || cof_globals_client != cl || slot < 0 ) return;
	CoF_GlobalsSave( cof_globals_base, cof_globals_slots[slot] );
	CoF_GlobalsLoad( cof_globals_base, cof_globals_slots[0] ); // outside a command: the host's
	cof_globals_client = NULL;
}

// A new player in a slot starts from nothing (story frame).
void SV_CoF_GlobalsFrame( void )
{
	int i;
	for( i = 1; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		if( svs.clients[i].state == cs_spawned ) continue;
		memset( cof_globals_slots[i], 0, sizeof( cof_globals_slots[i] ));
		cof_globals_room_type[i] = 0.0f;
	}
}

static int CoF_RoomTypeSlot( const char *name )
{
	sv_client_t *cl = cof_globals_client;
	qboolean host = cl && NET_IsLocalAddress( cl->netchan.remote_address ) && !FBitSet( cl->flags, FCL_FAKECLIENT );
	return CoF_RoomTypeOwner( name, CoF_GlobalsSlot( cl ), host, Cvar_FindVar( "room_type" ) != NULL );
}

qboolean SV_CoF_RoomTypeGet( const char *name, float *value )
{
	int slot = CoF_RoomTypeSlot( name );
	if( slot < 0 ) return false;
	*value = cof_globals_room_type[slot];
	return true;
}

qboolean SV_CoF_RoomTypeSet( const char *name, float value )
{
	sv_client_t *cl = cof_globals_client;
	int slot = CoF_RoomTypeSlot( name );
	if( slot < 0 ) return false;
	if( cof_globals_room_type[slot] == value ) return true;
	cof_globals_room_type[slot] = value;
	if( !FBitSet( cl->flags, FCL_FAKECLIENT ) && !NET_IsLocalAddress( cl->netchan.remote_address ))
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_roomtype );
		MSG_WriteShort( &cl->netchan.message, (int)value );
	}
	return true;
}
