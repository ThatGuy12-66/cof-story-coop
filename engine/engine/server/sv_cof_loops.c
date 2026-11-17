/*
sv_cof_loops.c - Cry of Fear story co-op: a merged map's ambient loops for its own section's players (sv_cof_loops.h)
GPL-3.0-or-later.

- The game's sounds of a looping ambient_generic are recorded: one made while the map loads or a save restores it is
  held back from the sign-on, a later start or ramp goes to its section's players only, a stop to everyone as always.
- A player's first frame in the map starts their section's loops; a move to another section stops the left
  section's and starts the new one's, to that player only.
- A save's sound list holds the host's own loops: they are left out, the restored ambient_generic plays its own again.
Log: phase=loops-moved userid from to stopped started.
*/
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_story.h"
#include "sv_cof_sections.h"
#include "sv_cof_loops.h"

#define COF_AMBIENT_NOT_LOOPING 32 // AMBIENT_SOUND_NOT_LOOPING (util.h)
#define COF_LOOP_MESSAGE 32 // room one sound message needs in a reliable buffer

static cof_loops_t cof_loops;
static int cof_loops_spawn = -1; // the svs.spawncount the loops are of
static struct { int userid; int section; } cof_loop_players[MAX_CLIENTS];

// The loops are the running map's: a new map, or a save loaded, starts with none and every player unseen.
static void CoF_LoopsCurrent( void )
{
	int i;
	if( cof_loops_spawn == svs.spawncount ) return;
	cof_loops_spawn = svs.spawncount;
	cof_loops.count = 0;
	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		cof_loop_players[i].userid = -1;
		cof_loop_players[i].section = COF_LOOP_UNSEEN;
	}
}

static qboolean CoF_LoopsOn( const vec3_t origin )
{
	return origin && SV_CoF_StoryCoopEnabled() && SV_CoF_PointSection( origin ) >= 0;
}

static qboolean CoF_LoopEntity( const edict_t *ent )
{
	return SV_IsValidEdict( ent ) && !Q_strcmp( SV_GetString( ent->v.classname ), "ambient_generic" )
		&& !FBitSet( ent->v.spawnflags, COF_AMBIENT_NOT_LOOPING );
}

int SV_CoF_LoopSound( edict_t *ent, const float *pos, const char *sample, float vol, float attn, int flags, int pitch )
{
	cof_loop_t loop;
	int section, now;
	if( COM_StringEmptyOrNULL( sample ) || Q_strlen( sample ) >= COF_LOOP_SAMPLE || !CoF_LoopEntity( ent ) || !CoF_LoopsOn( pos ))
		return -1;
	CoF_LoopsCurrent();
	if( FBitSet( flags, SND_STOP ))
	{
		CoF_LoopStop( &cof_loops, NUM_FOR_EDICT( ent ), sample );
		return -1;
	}
	section = SV_CoF_PointSection( pos );
	now = FBitSet( flags, SND_SPAWNING ) ? COF_LOOP_HELD : section;
	if( FBitSet( flags, SND_CHANGE_VOL | SND_CHANGE_PITCH ))
		return CoF_LoopChange( &cof_loops, NUM_FOR_EDICT( ent ), sample, FBitSet( flags, SND_CHANGE_VOL ) ? bound( 0, (int)( vol * 255 ), 255 ) : -1,
			FBitSet( flags, SND_CHANGE_PITCH ) ? bound( 0, pitch, 255 ) : -1 ) ? now : -1;
	memset( &loop, 0, sizeof( loop ));
	loop.entnum = NUM_FOR_EDICT( ent );
	loop.serial = ent->serialnumber;
	loop.section = section;
	Q_strncpy( loop.sample, sample, sizeof( loop.sample ));
	VectorCopy( pos, loop.pos );
	loop.vol = bound( 0, (int)( vol * 255 ), 255 );
	loop.attn = attn;
	loop.pitch = bound( 0, pitch, 255 );
	return CoF_LoopStart( &cof_loops, &loop ) ? now : -1; // not recorded: as the engine always sent it
}

qboolean SV_CoF_LoopRestored( edict_t *ent, int channel )
{
	return channel == CHAN_STATIC && CoF_LoopEntity( ent ) && CoF_LoopsOn( ent->v.origin );
}

// false: no room in the player's reliable buffer
static qboolean CoF_LoopSend( sv_client_t *cl, const cof_loop_t *loop, qboolean stop )
{
	if( MSG_GetNumBytesLeft( &cl->netchan.message ) < COF_LOOP_MESSAGE ) return false;
	if( stop ) SV_BuildSoundMsgNum( &cl->netchan.message, loop->entnum, CHAN_STATIC, loop->sample, 0, 0.0f, SND_STOP, 0, loop->pos );
	else SV_BuildSoundMsgNum( &cl->netchan.message, loop->entnum, CHAN_STATIC, loop->sample, loop->vol, loop->attn, 0, loop->pitch, loop->pos );
	return true;
}

void SV_CoF_LoopsFrame( void )
{
	int i, j;
	CoF_LoopsCurrent();
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		int from, to, stopped = 0, started = 0;
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
		if(( to = SV_CoF_PlayerSection( cl->edict )) < 0 ) return; // not a merged map
		if( cof_loop_players[i].userid != cl->userid )
		{
			cof_loop_players[i].userid = cl->userid;
			cof_loop_players[i].section = COF_LOOP_UNSEEN;
		}
		if(( from = cof_loop_players[i].section ) == to ) continue;
		for( j = 0; j < cof_loops.count; j++ )
		{
			const cof_loop_t *loop = &cof_loops.loop[j];
			const edict_t *ent = SV_EdictNum( loop->entnum );
			int need = CoF_LoopMove( loop, SV_IsValidEdict( ent ) && ent->serialnumber == loop->serial, from, to );
			if( need == COF_LOOP_KEEP ) continue;
			if( !CoF_LoopSend( cl, loop, need == COF_LOOP_SEND_STOP )) break;
			if( need == COF_LOOP_SEND_STOP ) stopped++;
			else started++;
		}
		if( j < cof_loops.count ) continue; // the reliable buffer is full: the whole move again next frame (a start again is harmless)
		cof_loop_players[i].section = to;
		if( stopped || started )
			Con_Printf( "cofstory phase=loops-moved userid=%d from=%d to=%d stopped=%d started=%d\n", cl->userid, from, to, stopped, started );
	}
}
