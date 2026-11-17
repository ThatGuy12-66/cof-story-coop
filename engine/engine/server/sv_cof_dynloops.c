/*
sv_cof_dynloops.c - Cry of Fear co-op 1.4: looping sounds on a dynamic channel, stopped with their entity
(sv_cof_dynloops.h has the why). GPL-3.0-or-later.

- SV_StartSound tells it each sound sent (SV_CoF_DynLoopSent): a start of a looping sample on a channel other than
  CHAN_STATIC and CHAN_STREAM counts one more copy for that entity, channel and sample; the game's stop of it is sent
  again for each other copy, and the entry goes.
- SV_FreeEdict (SV_CoF_DynLoopsFreed): an entity freed while the map runs has its copies stopped on every client.
- A new map, or a save loaded, starts with none.
Logs: phase=dynloop-stopped (a stop repeated, or a freed entity's), phase=dynloops-full (once a map).
*/
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_dynloops.h"

static cof_dynloops_t cof_dynloops;
static int cof_dynloops_spawn = -1;           // the svs.spawncount the list is of
static signed char cof_dynloops_wav[MAX_SOUNDS]; // per precache index this map: 1 loops, -1 not, 0 not read yet
static qboolean cof_dynloops_full_told;

static void CoF_DynLoopsCurrent( void )
{
	if( cof_dynloops_spawn == svs.spawncount ) return;
	cof_dynloops_spawn = svs.spawncount;
	cof_dynloops.count = 0;
	memset( cof_dynloops_wav, 0, sizeof( cof_dynloops_wav ));
	cof_dynloops_full_told = false;
}

static int CoF_DynLoopRead( void *ctx, void *buf, int len )
{
	return (int)FS_Read( (file_t *)ctx, buf, len );
}

// moved on exactly len bytes (told by where it is, whatever FS_Seek returns; a seek past the end fails)
static int CoF_DynLoopSkip( void *ctx, int len )
{
	fs_offset_t before = FS_Tell( (file_t *)ctx );
	FS_Seek( (file_t *)ctx, len, SEEK_CUR );
	return FS_Tell( (file_t *)ctx ) == before + len;
}

// The precached sample loops (its wav has a cue chunk): read once a map, chunk headers only.
static qboolean CoF_DynLoopLoops( int sound )
{
	const char *name;
	file_t *f;
	int cue = 0;
	if( sound <= 0 || sound >= MAX_SOUNDS ) return false;
	if( cof_dynloops_wav[sound] ) return cof_dynloops_wav[sound] > 0;
	name = sv.sound_precache[sound];
	if( name[0] && name[0] != '*' && name[0] != '!' && ( f = FS_Open( va( DEFAULT_SOUNDPATH "%s", name ), "rb", false )) != NULL )
	{
		cue = CoF_DynLoopWavCue( CoF_DynLoopRead, CoF_DynLoopSkip, f );
		FS_Close( f );
	}
	cof_dynloops_wav[sound] = cue ? 1 : -1;
	return cue != 0;
}

static qboolean CoF_DynLoopChannel( int chan, const char *sample )
{
	return chan != CHAN_STATIC && chan != CHAN_STREAM && !COM_StringEmptyOrNULL( sample ) &&
		sample[0] != '!' && sample[0] != '#' && sample[0] != '*';
}

void SV_CoF_DynLoopSent( edict_t *ent, int chan, const char *sample, int flags )
{
	int sound, entnum, copies;
	if( sv.state != ss_active || !SV_IsValidEdict( ent ) || !CoF_DynLoopChannel( chan, sample )) return;
	CoF_DynLoopsCurrent();
	if(( sound = SV_SoundIndex( sample )) <= 0 ) return; // sent already: no late precache here
	entnum = NUM_FOR_EDICT( ent );
	if( FBitSet( flags, SND_STOP ))
	{
		copies = CoF_DynLoopStopped( &cof_dynloops, entnum, ent->serialnumber, chan, sound );
		if( copies > 1 )
		{
			int i;
			for( i = 1; i < copies; i++ ) SV_CoF_StopSoundNum( entnum, chan, sample, ent->v.origin );
			Con_Printf( "cofstory phase=dynloop-stopped ent=%d class=%s sample=%s copies=%d why=stop\n", entnum,
				SV_GetString( ent->v.classname ), sample, copies );
		}
		return;
	}
	if( FBitSet( flags, SND_CHANGE_VOL | SND_CHANGE_PITCH ) || !CoF_DynLoopLoops( sound )) return;
	if( !CoF_DynLoopStarted( &cof_dynloops, entnum, ent->serialnumber, chan, sound ) && !cof_dynloops_full_told )
	{
		cof_dynloops_full_told = true;
		Con_Printf( "cofstory phase=dynloops-full count=%d\n", cof_dynloops.count );
	}
}

void SV_CoF_DynLoopsFreed( edict_t *ent )
{
	int entnum, at = 0;
	if( !ent || ent->free || cof_dynloops_spawn != svs.spawncount || !cof_dynloops.count ) return;
	entnum = NUM_FOR_EDICT( ent );
	while(( at = CoF_DynLoopOfEntity( &cof_dynloops, entnum, ent->serialnumber, at )) >= 0 )
	{
		const cof_dynloop_t *e = &cof_dynloops.loops[at];
		if( sv.state == ss_active && e->sound > 0 && e->sound < MAX_SOUNDS && sv.sound_precache[e->sound][0] )
		{
			int i;
			for( i = 0; i < e->copies; i++ ) SV_CoF_StopSoundNum( entnum, e->chan, sv.sound_precache[e->sound], ent->v.origin );
			Con_Printf( "cofstory phase=dynloop-stopped ent=%d class=%s sample=%s copies=%d why=freed\n", entnum,
				SV_GetString( ent->v.classname ), sv.sound_precache[e->sound], e->copies );
		}
		CoF_DynLoopRemove( &cof_dynloops, at ); // the last entry moved here: look at this place again
	}
}
