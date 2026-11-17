/*
sv_cof_view.c - Cry of Fear story co-op: a merged map's view distance, each section its own
GPL-3.0-or-later.

hl.dll sets sv_zmax from the worldspawn's MaxRange, and a merged map's worldspawn takes the largest of its members',
so the short view distances that make some maps dark (c_forest4 1500) were lost in a merged map. zmax reaches a client
in the movevars, whose deltas each client applies to what it has: with cof_coop_section_view (default 1) every client
in a merged map gets a delta with only its section's MaxRange (cof_sections.h CoF_SectMaxRange) whenever that differs
from what it was last sent. Research: docs/research/2026-10-18-cof-decompilation-survey.md sections 3 and 5.
Log: phase=view-distance userid section zmax.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_sections.h"
#include "net_encode.h"

static CVAR_DEFINE_AUTO( cof_coop_section_view, "1", 0, "Cry of Fear story co-op: each section of a merged map has its own map's view distance (MaxRange); 0: the largest of them everywhere" );

static float cof_view_sent[MAX_CLIENTS]; // the zmax a client was sent (0: the server's, from its connection)
static int cof_view_userid[MAX_CLIENTS];

void SV_CoF_ViewInit( void )
{
	Cvar_RegisterVariable( &cof_coop_section_view );
}

void SV_CoF_ViewFrame( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		movevars_t from, to;
		float wanted;
		int section;
		if( cl->state != cs_spawned || cof_view_userid[i] != cl->userid )
		{
			cof_view_sent[i] = 0.0f; // a new connection gets the server's movevars in full
			cof_view_userid[i] = cl->state == cs_spawned ? cl->userid : 0;
			if( cl->state != cs_spawned ) continue;
		}
		if( FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) || !SV_CoF_StoryCoopEnabled( )) continue;
		section = SV_CoF_PlayerSection( cl->edict );
		wanted = cof_coop_section_view.value != 0.0f && section >= 0 ? SV_CoF_SectionMaxRange( section ) : 0.0f;
		if( wanted <= 0.0f ) wanted = svgame.movevars.zmax;
		if( wanted == ( cof_view_sent[i] > 0.0f ? cof_view_sent[i] : svgame.movevars.zmax )) continue;
		from = svgame.movevars;
		from.zmax = cof_view_sent[i] > 0.0f ? cof_view_sent[i] : svgame.movevars.zmax;
		to = svgame.movevars;
		to.zmax = wanted;
		if( MSG_WriteDeltaMovevars( &cl->netchan.message, &from, &to ))
			Con_Printf( "cofstory phase=view-distance userid=%d section=%d zmax=%g\n", cl->userid, section, wanted );
		cof_view_sent[i] = wanted;
	}
}
