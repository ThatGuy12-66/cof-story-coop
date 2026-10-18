/*
sv_cof_voice.c - Cry of Fear story co-op: Simon's voice instead of the police officer's (sv_cof_voice.h)
GPL-3.0-or-later.

A player's police sample becomes a random one of Simon's in SV_StartSound; when the game precaches a police sample,
the engine precaches Simon's set with it. Not for the doctor (the doctor campaign, pev->vuser1.x, has its own set,
which multiplayer never precaches), and never for a stop.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_voice.h"

static CVAR_DEFINE_AUTO( cof_simon_voice, "1", 0, "Cry of Fear story co-op: players speak with Simon's voice (pain, death, breathing...), as in single player; 0: the police officer's, as the game does in multiplayer" );

void SV_CoF_VoiceInit( void )
{
	Cvar_RegisterVariable( &cof_simon_voice );
}

static qboolean CoF_VoiceOn( void )
{
	return cof_simon_voice.value != 0.0f && SV_CoF_StoryCoopEnabled();
}

const char *SV_CoF_VoiceSample( edict_t *ent, const char *sample, int flags, char *buf, size_t size )
{
	int set = CoF_VoiceSet( sample );
	if( set < 0 || FBitSet( flags, SND_STOP ) || !SV_IsValidEdict( ent ) || !FBitSet( ent->v.flags, FL_CLIENT ) ||
		ent->v.vuser1[0] != 0.0f || !CoF_VoiceOn( ))
		return sample;
	CoF_VoiceName( set, COM_RandomLong( 0, CoF_VoiceCount( set ) - 1 ), buf, size );
	return buf[0] ? buf : sample;
}

void SV_CoF_VoicePrecache( const char *name )
{
	char simon[MAX_QPATH];
	int set = CoF_VoiceSet( name ), n;
	if( set < 0 || !CoF_VoiceOn( )) return;
	for( n = 0; n < CoF_VoiceCount( set ); n++ )
	{
		CoF_VoiceName( set, n, simon, sizeof( simon ));
		if( simon[0] ) SV_SoundIndex( simon );
	}
}
