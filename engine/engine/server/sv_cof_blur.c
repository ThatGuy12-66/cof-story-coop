/*
sv_cof_blur.c - Cry of Fear story co-op: one screen blur for the whole party (sv_cof_blur.h)
GPL-3.0-or-later.

After the game's frame, every spawned player's m_iBlurActive is read through the retail layer; a flip sets the
others, a player who joins or arrives (or whose morphine trip ends) takes the party's. Writing the field is enough:
the game's UpdateClientData sends "Blur" when it changes. A story level change from a map that does not hand the blur
on clears everyone's before the level is saved (sv_save.c), and a checkpoint loaded on one starts the party off
(sv_cof_session.inc). With cof_coop_shared_blur 0 none of this happens: each flip stays the player's who fired it, as
in single player. Log: phase=blur-shared userid blur reason (flip, join, trip-end) from (the flipper's userid, 0 for
none); phase=blur-cleared userid map; the "Blur" message itself is in the trace (sv_cof_trace.c).
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_blur.h"

static CVAR_DEFINE_AUTO( cof_coop_shared_blur, "1", 0, "Cry of Fear story co-op: the story's screen blur (cof_blur) is one for the whole party, and a player who joins or arrives takes it; 0: each flip only for the player who fired it, as in single player" );

static cof_blur_state_t cof_blur;
static int cof_blur_generation = -1, cof_blur_checkpoint = -1;

static const char *CoF_BlurWhy( int why )
{
	switch( why )
	{
	case COF_BLUR_FLIP: return "flip";
	case COF_BLUR_JOIN: return "join";
	case COF_BLUR_TRIP_END: return "trip-end";
	default: return "keep";
	}
}

void SV_CoF_BlurInit( void )
{
	Cvar_RegisterVariable( &cof_coop_shared_blur );
}

void SV_CoF_BlurCheckpointLoaded( void )
{
	cof_blur_checkpoint = svs.spawncount;
}

void SV_CoF_BlurLeaving( const char *map )
{
	int i, blur, trip;
	if( !SV_CoF_StoryCoopEnabled() || cof_coop_shared_blur.value == 0.0f || CoF_BlurCarried( map )) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		// a trip goes on in the next map, with the blur it holds
		if( !SV_CoF_RetailBlur( &svs.clients[i], &blur, &trip ) || !blur || trip ||
			!SV_CoF_RetailSetBlur( &svs.clients[i], 0 )) continue;
		Con_Printf( "cofstory phase=blur-cleared userid=%d map=%s\n", svs.clients[i].userid, map );
	}
}

void SV_CoF_BlurFrame( void )
{
	cof_blur_now_t now[COF_BLUR_PLAYERS];
	int set[COF_BLUR_PLAYERS], why[COF_BLUR_PLAYERS];
	int count = Q_min( svs.maxclients, COF_BLUR_PLAYERS ), i, source;
	if( cof_blur_generation != svs.spawncount || cof_coop_shared_blur.value == 0.0f )
	{
		// a new map, or off: start again from what the players bring, or off after a checkpoint
		cof_blur_generation = svs.spawncount;
		CoF_BlurReset( &cof_blur, cof_blur_checkpoint == svs.spawncount && !CoF_BlurCarried( sv.name ));
		if( cof_coop_shared_blur.value == 0.0f ) return;
	}
	for( i = 0; i < count; i++ )
		now[i].present = SV_CoF_RetailBlur( &svs.clients[i], &now[i].blur, &now[i].trip );
	source = CoF_BlurShare( &cof_blur, now, count, set, why );
	for( i = 0; i < count; i++ )
	{
		if( set[i] < 0 || !SV_CoF_RetailSetBlur( &svs.clients[i], set[i] )) continue;
		Con_Printf( "cofstory phase=blur-shared userid=%d blur=%d reason=%s from=%d\n", svs.clients[i].userid, set[i],
			CoF_BlurWhy( why[i] ), source >= 0 ? svs.clients[source].userid : 0 );
	}
}
