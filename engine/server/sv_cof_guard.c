/*
sv_cof_guard.c - Cry of Fear story co-op: client commands the retail game trusts too much (sv_cof_guard.h)
GPL-3.0-or-later.

Log: phase=cmd-refused userid cmd reason.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_guard.h"

qboolean SV_CoF_GuardClientCommand( sv_client_t *cl )
{
	qboolean host = NET_IsLocalAddress( cl->netchan.remote_address ) && !FBitSet( cl->flags, FCL_FAKECLIENT );
	const char *reason = CoF_GuardRefusal( Cmd_Argv( 0 ), SV_CoF_StoryCoopEnabled(), host );
	if( !reason ) return false;
	Con_Printf( "cofstory phase=cmd-refused userid=%d cmd=%s reason=%s\n", cl->userid, Cmd_Argv( 0 ), reason );
	return true;
}
