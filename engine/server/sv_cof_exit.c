/* Cry of Fear story co-op: hold a level exit until the party is at it.
 * GPL-3.0-or-later. The engine side of sv_cof_exit_state.h: it says who
 * stands in the exit, stores a held request once, prompts both players and
 * replays the request when everyone is there. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_party.h"
#include "sv_cof_retail.h"
#include "sv_cof_exit_state.h"
#include "sv_cof_exit.h"
#include "sv_cof_companion.h"

#define COF_EXIT_MODELS 16

static cof_exit_state_t cof_exit;
static qboolean cof_exit_scripted;
static int cof_exit_models[COF_EXIT_MODELS], cof_exit_model_count;
static char cof_exit_unholdable[COF_EXIT_NAME]; // already reported in this map
static int cof_exit_unholdable_generation = -1;
static int cof_exit_gone_generation = -1; // the hold was released in this map: the trigger still being touched must not start another
// A second exit turned away while another hold came first: already read and
// found holdable, so its trigger asking every frame does not read the map again.
static char cof_exit_turned_level[COF_EXIT_NAME], cof_exit_turned_landmark[COF_EXIT_NAME];
static int cof_exit_turned_generation = -1;

void SV_CoF_ExitReset( void )
{
	CoF_ExitClear( &cof_exit );
	cof_exit_scripted = false;
	cof_exit_model_count = 0;
	cof_exit_unholdable_generation = -1;
	cof_exit_gone_generation = -1;
	cof_exit_turned_generation = -1;
}

// Everyone who counts, by client slot, against the exit last asked for.
static int CoF_ExitPlayers( cof_exit_player_t *players )
{
	int i, n = Q_min( svs.maxclients, COF_EXIT_PLAYERS );
	memset( players, 0, sizeof( *players ) * COF_EXIT_PLAYERS );
	for( i = 0; i < n; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_exit_player_t *p = &players[i];
		// A companion bot, or a second client the AI drives while nobody plays it
		// (solo testing), comes along with the transition; it holds nothing.
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT ) || SV_CoF_CompanionAway( cl )) continue;
		p->userid = cl->userid;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue; // joining: holds the exit
		p->spawned = 1;
		p->alive = cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
		p->touching = p->alive && SV_CoF_PartyInExit( cl->edict, cof_exit_models, cof_exit_model_count );
		VectorCopy( cl->edict->v.origin, p->origin );
	}
	return n;
}

static const char *CoF_ExitName( int userid )
{
	int i;
	for( i = 0; userid && i < svs.maxclients; i++ )
		if( svs.clients[i].state >= cs_connected && svs.clients[i].userid == userid ) return svs.clients[i].name;
	return "your partner";
}

// A hold that belongs to an earlier map is dead even before the frame notices.
static qboolean CoF_ExitLive( void )
{
	return cof_exit.active && cof_exit.generation == svs.spawncount;
}

// First come, first served: another exit's request waits while the held
// exit's waiter still stands in it. Asked against the held exit's own models,
// before they are replaced by the other exit's.
static qboolean CoF_ExitTurnAway( const char *map, const char *landmark )
{
	cof_exit_player_t players[COF_EXIT_PLAYERS];
	int n;
	if( !CoF_ExitLive()) return false;
	if( cof_exit_scripted ) return true;
	n = CoF_ExitPlayers( players );
	return CoF_ExitTaken( &cof_exit, players, n, svs.spawncount, map, landmark );
}

qboolean SV_CoF_ExitRequest( const char *level, const char *landmark )
{
	cof_exit_player_t players[COF_EXIT_PLAYERS];
	cof_exit_state_t was = cof_exit;
	char map[COF_EXIT_NAME];
	int models[COF_EXIT_MODELS], count, n, began;
	qboolean fires = false, scripted = false, same;
	vec3_t spot;

	// Landmark-less requests are the game's own restarts and co-op doors: never held.
	if( !SV_CoF_StoryCoopEnabled() || COM_StringEmptyOrNULL( level ) || COM_StringEmptyOrNULL( landmark )) return true;
	if( Q_strlen( level ) >= sizeof( map )) return true;
	Q_strncpy( map, level, sizeof( map ));
	COM_StripExtension( map );
	if( cof_exit_gone_generation == svs.spawncount ) return true;
	same = CoF_ExitLive() && !Q_strcmp( cof_exit.level, map ) && !Q_strcmp( cof_exit.landmark, landmark );
	if( !same && CoF_ExitLive() && cof_exit_scripted ) return false;
	if( !same && cof_exit_turned_generation == svs.spawncount && !Q_strcmp( cof_exit_turned_level, map ) &&
		!Q_strcmp( cof_exit_turned_landmark, landmark ) && CoF_ExitTurnAway( map, landmark ))
		return false;
	if( same )
	{
		// the game asks again every frame of a hold: reuse what the first request found
		count = cof_exit_model_count;
		scripted = cof_exit_scripted;
		VectorCopy( cof_exit.spot, spot );
	}
	else
	{
		// An exit the engine is going to refuse is not worth waiting at.
		if( !SV_CoF_LiveLandmark( landmark, spot )) return true;
		count = SV_CoF_MapExitModels( sv.name, map, landmark, models, COF_EXIT_MODELS, &fires, &scripted );
	}
	if( fires )
	{
		// Holding this exit would repeat side effects on every request: its own
		// targets, or a firing trigger's killtarget or namesakes. No story exit
		// has any today.
		if( cof_exit_unholdable_generation != svs.spawncount || Q_strcmp( cof_exit_unholdable, map ))
			Con_Printf( "cofstory phase=exit-unholdable destination=%s landmark=%s\n", map, landmark );
		Q_strncpy( cof_exit_unholdable, map, sizeof( cof_exit_unholdable ));
		cof_exit_unholdable_generation = svs.spawncount;
		CoF_ExitClear( &cof_exit );
		return true;
	}
	if( !same )
	{
		if( !scripted && CoF_ExitTurnAway( map, landmark ))
		{
			Q_strncpy( cof_exit_turned_level, map, sizeof( cof_exit_turned_level ));
			Q_strncpy( cof_exit_turned_landmark, landmark, sizeof( cof_exit_turned_landmark ));
			cof_exit_turned_generation = svs.spawncount;
			return false;
		}
		// The newer request replaces the held one. Cleared here, because a
		// waiter who walked from that exit into this one would otherwise count
		// as still standing in it once the models below are this exit's.
		CoF_ExitClear( &cof_exit );
		memcpy( cof_exit_models, models, sizeof( models ));
		cof_exit_model_count = count;
		cof_exit_scripted = scripted;
	}
	n = CoF_ExitPlayers( players );
	if( CoF_ExitRequest( &cof_exit, players, n, svs.spawncount, map, landmark, spot, sv.time, &began ) == COF_EXIT_GO )
	{
		if( same ) // this exit's own hold, not one it replaced
			Con_Printf( "cofstory phase=exit-ready destination=%s landmark=%s waiter=%d waited=%.1f\n", map, landmark, was.waiter, sv.time - was.began );
		return true;
	}
	if( scripted ) cof_exit.walkin = 0;
	if( began )
		Con_Printf( "cofstory phase=exit-hold destination=%s landmark=%s waiter=%d walkin=%d missing=%d\n", map, landmark,
			cof_exit.waiter, cof_exit.walkin, CoF_ExitMissing( players, n, cof_exit.waiter ));
	return false;
}

// Replay the held request. The transition's own capture and arrival regroup
// (sv_cof_party.c) place a partner who is not beside the leader.
static void CoF_ExitGo( const char *phase )
{
	char level[COF_EXIT_NAME], landmark[COF_EXIT_NAME];
	Q_strncpy( level, cof_exit.level, sizeof( level ));
	Q_strncpy( landmark, cof_exit.landmark, sizeof( landmark ));
	Con_Printf( "cofstory phase=%s destination=%s landmark=%s waiter=%d waited=%.1f\n", phase, level, landmark, cof_exit.waiter, sv.time - cof_exit.began );
	CoF_ExitClear( &cof_exit );
	cof_exit_gone_generation = svs.spawncount;
	SV_QueueChangeLevel( level, landmark );
}

// The words carry the meaning: the colour index did not render in the live check.
static void CoF_ExitPrompts( const cof_exit_player_t *players, int n )
{
	const cof_exit_player_t *waiter = CoF_ExitFind( players, n, cof_exit.waiter );
	const char *missing = CoF_ExitName( CoF_ExitMissing( players, n, cof_exit.waiter ));
	char text[160];
	int i;
	for( i = 0; i < n; i++ )
	{
		// a dead player has the game's own respawn prompt and cannot warp
		if( !players[i].userid || !players[i].spawned || !players[i].alive ) continue;
		if( CoF_ExitAt( &players[i], waiter ))
		{
			if( sv.time - cof_exit.began >= COF_EXIT_ESCAPE )
				Q_snprintf( text, sizeof( text ), "Please wait until everyone is ready! (press J to leave without %s)", missing );
			else Q_snprintf( text, sizeof( text ), "Please wait until everyone is ready! (%s can press J to join you)", missing );
			SV_CoF_RetailProFont( &svs.clients[i], 2, text, false );
		}
		else
		{
			Q_snprintf( text, sizeof( text ), "%s is at the exit. Press J to join.", CoF_ExitName( cof_exit.waiter ));
			SV_CoF_RetailProFont( &svs.clients[i], 1, text, false );
		}
	}
}

void SV_CoF_ExitFrame( void )
{
	cof_exit_player_t players[COF_EXIT_PLAYERS];
	int n, waiter = cof_exit.waiter;
	if( !cof_exit.active ) return;
	n = CoF_ExitPlayers( players );
	switch( CoF_ExitFrame( &cof_exit, players, n, svs.spawncount, sv.time,
		!cof_exit_scripted && SV_CoF_PartyExitRefires( cof_exit_models, cof_exit_model_count )))
	{
	case COF_EXIT_READY:
		CoF_ExitGo( "exit-ready" );
		break;
	case COF_EXIT_CANCELLED:
		Con_Printf( "cofstory phase=exit-cancelled waiter=%d\n", waiter );
		break;
	case COF_EXIT_WAIT:
		if( CoF_ExitPromptDue( &cof_exit, sv.time )) CoF_ExitPrompts( players, n );
		break;
	}
}

qboolean SV_CoF_ExitWarpKey( sv_client_t *cl )
{
	char text[96];
	int left, answer;
	if( !CoF_ExitLive()) return false;
	answer = CoF_ExitEscape( &cof_exit, cl->userid, sv.time, &left );
	if( !answer ) return false;
	if( answer > 0 )
	{
		CoF_ExitGo( "exit-forced" ); // the escape hatch: leave without the partner
		return true;
	}
	Q_snprintf( text, sizeof( text ), "You can leave without your partner in %d second%s.", left, left == 1 ? "" : "s" );
	SV_CoF_RetailProFont( cl, 2, text, false );
	return true;
}

int SV_CoF_ExitWaiter( void )
{
	return CoF_ExitLive() ? cof_exit.waiter : 0;
}

// The live hold is for this exit (the game asks for it again every frame while it is held).
qboolean SV_CoF_ExitHeld( const char *level, const char *landmark )
{
	char map[COF_EXIT_NAME];
	if( !CoF_ExitLive() || COM_StringEmptyOrNULL( level ) || COM_StringEmptyOrNULL( landmark ) || Q_strlen( level ) >= sizeof( map )) return false;
	Q_strncpy( map, level, sizeof( map ));
	COM_StripExtension( map );
	return !Q_strcmp( cof_exit.level, map ) && !Q_strcmp( cof_exit.landmark, landmark );
}

qboolean SV_CoF_ExitQuietAlert( const char *text )
{
	return CoF_ExitLive() && !Q_strncmp( text, "CHANGE LEVEL: ", 14 );
}
