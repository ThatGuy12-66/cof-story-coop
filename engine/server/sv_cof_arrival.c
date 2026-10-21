/*
sv_cof_arrival.c - Cry of Fear story co-op: what a story level change leaves behind (sv_cof_arrival.h)
GPL-3.0-or-later.

At every story sign-on: the player's HideWeapon (hands), everyone's ScoreInfo (torches), a door the player was in
the middle of let go. Every frame: the co-op torch as each player last switched it. Each exit request: whether the
players standing in it count (the exit behind a party that just arrived).
Logs: phase=arrival-hud userid hidehud scores, phase=arrival-door-finished userid, phase=torch-kept userid frags,
phase=torch-switched userid on, phase=exit-arrival-refused destination landmark userid reason.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_party.h"
#include "sv_cof_exit.h"
#include "sv_cof_arrival.h"

#define COF_ARRIVAL_PLAYER_SIZE 0x281C
#define COF_ARRIVAL_DOOR_FLAG   0x1EA4 // bool b_GoingThroughADoor (CInterDoor::StartSequence)
#define COF_ARRIVAL_HIDEHUD     0x2500 // int m_iHideHUD
#define COF_ARRIVAL_DOOR_HIDES  ( 1 | 8 ) // HIDEHUD_WEAPONS | HIDEHUD_HEALTH: what a door hides (CoF_InterDoor.cpp:347-348)
#define COF_ARRIVAL_EXIT_MODELS 16

typedef struct
{
	int userid;
	qboolean known, on;       // the torch as the player last switched it
	qboolean in_command;      // inside the game's F
	int before, frags;        // frags the game's F saw, and frags before the engine set it
	double door_seen;         // sv.time last seen in an inter_door
	int door_generation;
	qboolean stayed;          // in an exit ever since the party arrived
	int stay_generation;
	qboolean signon;          // a story sign-on: the HUD state goes out at the next frame
	double refused_logged;
} cof_arrival_player_t;

static cof_arrival_player_t cof_arrival_players[MAX_CLIENTS];
static struct { int deaths, pclass, team; } cof_arrival_scores[MAX_CLIENTS + 1];
static struct
{
	int generation;
	double time;
	vec3_t landmark;
} cof_arrival;
static struct { int userid, generation; } cof_arrival_carried[MAX_CLIENTS];
static struct
{
	int generation;
	char level[MAX_QPATH], landmark[MAX_QPATH];
	int models[COF_ARRIVAL_EXIT_MODELS], count;
	qboolean fires, scripted;
} cof_arrival_exit;

void SV_CoF_ArrivalReset( void )
{
	memset( cof_arrival_players, 0, sizeof( cof_arrival_players ));
	memset( cof_arrival_scores, 0, sizeof( cof_arrival_scores ));
	memset( &cof_arrival, 0, sizeof( cof_arrival ));
	memset( &cof_arrival_exit, 0, sizeof( cof_arrival_exit ));
	memset( cof_arrival_carried, 0, sizeof( cof_arrival_carried ));
	cof_arrival.generation = -1;
	cof_arrival_exit.generation = -1;
}

static int CoF_ArrivalSlot( const sv_client_t *cl )
{
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS ) return -1;
	return slot;
}

// The record of the player in this slot now: a new userid starts afresh.
static cof_arrival_player_t *CoF_ArrivalPlayer( const sv_client_t *cl )
{
	int slot = CoF_ArrivalSlot( cl );
	cof_arrival_player_t *p;
	if( slot < 0 ) return NULL;
	p = &cof_arrival_players[slot];
	if( p->userid != cl->userid )
	{
		memset( p, 0, sizeof( *p ));
		p->userid = cl->userid;
		p->door_generation = -1;
		p->stay_generation = -1;
	}
	return p;
}

static qboolean CoF_ArrivalHuman( const sv_client_t *cl )
{
	return cl->state == cs_spawned && !FBitSet( cl->flags, FCL_FAKECLIENT ) && SV_IsValidEdict( cl->edict );
}

static qboolean CoF_ArrivalAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO && !FBitSet( ent->v.flags, FL_KILLME );
}

// The scoreboard values the game last sent for a player; frags is ours to send.
void SV_CoF_ArrivalMessage( const char *name, const byte *payload, int size )
{
	int index;
	if( !name || !payload || size != COF_ARRIVAL_SCOREINFO_SIZE || Q_strcmp( name, "ScoreInfo" )) return;
	index = payload[0];
	if( index < 1 || index > MAX_CLIENTS ) return;
	cof_arrival_scores[index].deaths = CoF_ScoreInfoField( payload, 1 );
	cof_arrival_scores[index].pclass = CoF_ScoreInfoField( payload, 2 );
	cof_arrival_scores[index].team = CoF_ScoreInfoField( payload, 3 );
}

// One player's ScoreInfo with their frags now, to one client (to) or to every client (NULL).
static int CoF_ArrivalSendScore( int index, sv_client_t *to )
{
	byte payload[COF_ARRIVAL_SCOREINFO_SIZE];
	edict_t *ent = SV_EdictNum( index );
	int i, sent = 0;
	if( index < 1 || index > svs.maxclients || index > MAX_CLIENTS || !SV_IsValidEdict( ent )) return 0;
	CoF_ScoreInfoPayload( payload, index, (int)ent->v.frags, cof_arrival_scores[index].deaths,
		cof_arrival_scores[index].pclass, cof_arrival_scores[index].team );
	for( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if(( to && cl != to ) || !CoF_ArrivalHuman( cl )) continue;
		if( SV_CoF_SendUserMessage( cl->edict, "ScoreInfo", payload, sizeof( payload ), true )) sent++;
	}
	return sent;
}

// The restore of a player the story level change carried over (sv_save.c).
void SV_CoF_ArrivalRestored( const sv_client_t *cl )
{
	int slot = CoF_ArrivalSlot( cl );
	if( slot < 0 ) return;
	cof_arrival_carried[slot].userid = cl->userid;
	cof_arrival_carried[slot].generation = svs.spawncount;
}

// Carried over into this map and not signed on in it yet: dropping now leaves their items in the restored player.
qboolean SV_CoF_ArrivalLoading( const sv_client_t *cl )
{
	int slot = CoF_ArrivalSlot( cl );
	return slot >= 0 && cl->state != cs_spawned && cl->userid && cof_arrival_carried[slot].userid == cl->userid &&
		cof_arrival_carried[slot].generation == svs.spawncount;
}

// Called by SV_Begin_f for a player the story level change carried over.
void SV_CoF_ArrivalSignon( sv_client_t *cl )
{
	cof_arrival_player_t *p;
	if( !SV_CoF_StoryCoopEnabled() || !( p = CoF_ArrivalPlayer( cl ))) return;
	p->signon = true;
}

// Called when every carried player has signed on: who stands in an exit now, and the landmark they came in by.
void SV_CoF_ArrivalReady( const vec3_t landmark )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() ) return;
	cof_arrival.generation = svs.spawncount;
	cof_arrival.time = sv.time;
	VectorCopy( landmark, cof_arrival.landmark );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_arrival_player_t *p;
		if( !CoF_ArrivalHuman( cl ) || !( p = CoF_ArrivalPlayer( cl ))) continue;
		p->stayed = SV_CoF_PartyInExit( cl->edict, NULL, 0 );
		p->stay_generation = svs.spawncount;
	}
}

// A door the player was in the middle of when the level changed: its EndSequence stays in the old map.
static void CoF_ArrivalFinishDoor( sv_client_t *cl, byte *pv )
{
	int hide;
	if( pv[COF_ARRIVAL_DOOR_FLAG] != 1 ) return;
	pv[COF_ARRIVAL_DOOR_FLAG] = 0;
	memcpy( &hide, pv + COF_ARRIVAL_HIDEHUD, sizeof( hide ));
	hide &= ~COF_ARRIVAL_DOOR_HIDES;
	memcpy( pv + COF_ARRIVAL_HIDEHUD, &hide, sizeof( hide ));
	ClearBits( cl->edict->v.flags, FL_FROZEN|FL_NOTARGET );
	ClearBits( cl->edict->v.effects, EF_NODRAW );
	Con_Printf( "cofstory phase=arrival-door-finished userid=%d\n", cl->userid );
}

static void CoF_ArrivalHud( sv_client_t *cl )
{
	byte *pv = SV_CoF_StoryEntityPrivate( cl->edict, COF_ARRIVAL_PLAYER_SIZE );
	byte hide = 0;
	int i, scores = 0, value;
	if( pv )
	{
		CoF_ArrivalFinishDoor( cl, pv );
		memcpy( &value, pv + COF_ARRIVAL_HIDEHUD, sizeof( value ));
		hide = (byte)value;
		SV_CoF_SendUserMessage( cl->edict, "HideWeapon", &hide, 1, true );
	}
	for( i = 1; i <= svs.maxclients && i <= MAX_CLIENTS; i++ )
		if( svs.clients[i - 1].state >= cs_connected ) scores += CoF_ArrivalSendScore( i, cl );
	Con_Printf( "cofstory phase=arrival-hud userid=%d hidehud=%d scores=%d\n", cl->userid, pv ? hide : -1, scores );
}

void SV_CoF_ArrivalFrame( void )
{
	qboolean allowed = SV_CoF_CoopFlashlightAllowed();
	int i, keep;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_arrival_player_t *p;
		edict_t *ent = cl->edict;
		if( !CoF_ArrivalHuman( cl ) || !( p = CoF_ArrivalPlayer( cl ))) continue;
		if( p->signon )
		{
			p->signon = false;
			CoF_ArrivalHud( cl );
		}
		if( SV_CoF_PartyInDoor( cl ))
		{
			p->door_seen = sv.time;
			p->door_generation = svs.spawncount;
		}
		if( p->stayed && ( p->stay_generation != svs.spawncount || !SV_CoF_PartyInExit( ent, NULL, 0 )))
			p->stayed = false;
		if( !p->known )
		{
			p->known = true;
			p->on = ( (int)ent->v.frags & COF_ARRIVAL_TORCH ) != 0;
		}
		if( !allowed )
		{
			p->on = false; // sv_cof_light.c switches it off
			continue;
		}
		if( !CoF_ArrivalAlive( ent ) || ( keep = CoF_TorchKeep( p->on, (int)ent->v.frags )) < 0 ) continue;
		Con_Printf( "cofstory phase=torch-kept userid=%d frags=%d\n", cl->userid, (int)ent->v.frags );
		ent->v.frags = keep;
		CoF_ArrivalSendScore( i + 1, NULL );
	}
}

static cof_arrival_player_t *CoF_ArrivalTorchCommand( sv_client_t *cl )
{
	if( !SV_CoF_StoryCoopEnabled() || !SV_CoF_CoopFlashlightAllowed() || Q_strcmp( Cmd_Argv( 0 ), "flashlight" ) ||
		!CoF_ArrivalHuman( cl )) return NULL;
	return CoF_ArrivalPlayer( cl );
}

// Around the game's ClientCommand: F switches the torch the player has, not whatever the game left in frags.
void SV_CoF_ArrivalCommandBegin( sv_client_t *cl )
{
	cof_arrival_player_t *p = CoF_ArrivalTorchCommand( cl );
	if( !p ) return;
	if( !p->known )
	{
		p->known = true;
		p->on = ( (int)cl->edict->v.frags & COF_ARRIVAL_TORCH ) != 0;
	}
	p->in_command = true;
	p->frags = (int)cl->edict->v.frags;
	p->before = CoF_TorchBefore( p->on );
	cl->edict->v.frags = p->before;
}

void SV_CoF_ArrivalCommandEnd( sv_client_t *cl )
{
	cof_arrival_player_t *p = CoF_ArrivalTorchCommand( cl );
	int now;
	if( !p || !p->in_command ) return;
	p->in_command = false;
	if( !SV_IsValidEdict( cl->edict )) return;
	if(( now = CoF_TorchAfter( p->before, (int)cl->edict->v.frags )) < 0 )
	{
		cl->edict->v.frags = p->frags; // the game refused it (a camera, a wheelchair, dead): as it was
		return;
	}
	p->on = now;
	Con_Printf( "cofstory phase=torch-switched userid=%d on=%d\n", cl->userid, now );
}

static qboolean CoF_ArrivalExit( const char *level, const char *landmark )
{
	char map[MAX_QPATH];
	if( Q_strlen( level ) >= sizeof( map ) || Q_strlen( landmark ) >= sizeof( cof_arrival_exit.landmark )) return false;
	Q_strncpy( map, level, sizeof( map ));
	COM_StripExtension( map );
	if( cof_arrival_exit.generation == svs.spawncount && !Q_strcmp( cof_arrival_exit.level, map ) &&
		!Q_strcmp( cof_arrival_exit.landmark, landmark )) return true;
	cof_arrival_exit.generation = svs.spawncount;
	Q_strncpy( cof_arrival_exit.level, map, sizeof( cof_arrival_exit.level ));
	Q_strncpy( cof_arrival_exit.landmark, landmark, sizeof( cof_arrival_exit.landmark ));
	cof_arrival_exit.count = SV_CoF_MapExitModels( sv.name, map, landmark, cof_arrival_exit.models,
		COF_ARRIVAL_EXIT_MODELS, &cof_arrival_exit.fires, &cof_arrival_exit.scripted );
	return true;
}

// pfnChangeLevel: true when the players standing in this exit do not count yet (sv_cof_arrival.h, 3).
qboolean SV_CoF_ArrivalRefuses( const char *level, const char *landmark )
{
	cof_arrival_player_t *refused = NULL;
	double arrived_age = cof_arrival.generation == svs.spawncount ? sv.time - cof_arrival.time : -1.0;
	qboolean back = false;
	const char *reason;
	vec3_t spot;
	int i;
	if( !SV_CoF_StoryCoopEnabled() || COM_StringEmptyOrNULL( level ) || COM_StringEmptyOrNULL( landmark )) return false;
	if( SV_CoF_ExitWaiter( )) return false; // a held exit: its waiter already counted
	if( !CoF_ArrivalExit( level, landmark ) || cof_arrival_exit.count <= 0 || cof_arrival_exit.fires || cof_arrival_exit.scripted )
		return false;
	if( arrived_age >= 0.0 && SV_CoF_LiveLandmark( landmark, spot ))
	{
		vec3_t d;
		VectorSubtract( spot, cof_arrival.landmark, d );
		back = DotProduct( d, d ) < 1.0f;
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_arrival_player_t *p;
		double door_age;
		if( !CoF_ArrivalHuman( cl ) || !CoF_ArrivalAlive( cl->edict ) || !( p = CoF_ArrivalPlayer( cl )) ||
			!SV_CoF_PartyInExit( cl->edict, cof_arrival_exit.models, cof_arrival_exit.count )) continue;
		door_age = p->door_generation == svs.spawncount ? sv.time - p->door_seen : -1.0;
		if( SV_CoF_PartyInDoor( cl )) door_age = 0.0;
		if( CoF_ArrivalTouchCounts( door_age, p->stayed && p->stay_generation == svs.spawncount, arrived_age, back ))
			return false;
		if( !refused ) refused = p;
	}
	if( !refused ) return false; // nobody in it: a scripted change
	if( sv.time >= refused->refused_logged + 1.0 || sv.time < refused->refused_logged )
	{
		refused->refused_logged = sv.time;
		reason = refused->stayed ? "arrived-inside" : "just-arrived";
		Con_Printf( "cofstory phase=exit-arrival-refused destination=%s landmark=%s userid=%d reason=%s\n", level, landmark, refused->userid, reason );
	}
	return true;
}
