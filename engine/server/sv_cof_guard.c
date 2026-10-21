/*
sv_cof_guard.c - Cry of Fear story co-op: client commands the retail game trusts too much (sv_cof_guard.h)
GPL-3.0-or-later.

Log: phase=cmd-refused userid cmd reason.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_guard.h"

// 1.1.9 (K9): c_asylumb's bar puzzle is solved for good once its button says so (set by the solving think only).
static qboolean CoF_GuardPuzzleSolved( void )
{
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		if( SV_IsValidEdict( e ) && e->v.iuser1 && !Q_strcmp( SV_GetString( e->v.classname ), "cof_puzzlebarbutton" )) return true;
	}
	return false;
}

qboolean SV_CoF_GuardClientCommand( sv_client_t *cl )
{
	qboolean host = NET_IsLocalAddress( cl->netchan.remote_address ) && !FBitSet( cl->flags, FCL_FAKECLIENT );
	const char *reason = CoF_GuardRefusal( Cmd_Argv( 0 ), SV_CoF_StoryCoopEnabled(), host );
	edict_t *user = NULL;
	if( !reason && SV_CoF_StoryCoopEnabled( ))
		reason = CoF_GuardSceneRefusal( Cmd_Argv( 0 ), Cmd_Argv( 1 ), SV_CoF_RetailCameraHolds( cl ));
	if( !reason && SV_CoF_StoryCoopEnabled() && CoF_GuardPuzzleCommand( Cmd_Argv( 0 )))
		reason = CoF_GuardPuzzleRefusal( Cmd_Argv( 0 ), Cmd_Argv( 1 ), CoF_GuardPuzzleSolved( ));
	if( !reason && SV_CoF_StoryCoopEnabled() && CoF_GuardTelephoneCommand( Cmd_Argv( 0 )))
		reason = CoF_GuardTelephoneRefusal( Cmd_Argv( 0 ), SV_CoF_RetailTelephoneTaken( cl, &user ));
	if( !reason ) return false;
	Con_Printf( "cofstory phase=cmd-refused userid=%d cmd=%s reason=%s\n", cl->userid, Cmd_Argv( 0 ), reason );
	if( !Q_strcmp( reason, "host-pause" )) SV_ClientPrintf( cl, "Only the host can pause a co-op game.\n" );
	else if( !Q_strcmp( reason, "telephone-taken" ))
	{
		sv_client_t *other = user ? SV_ClientFromEdict( user, true ) : NULL;
		char text[96];
		if( other ) Q_snprintf( text, sizeof( text ), "%s has this phone now. Pick it up again to dial.", other->name );
		else Q_strncpy( text, "Pick up the phone again to dial.", sizeof( text ));
		SV_CoF_RetailProFont( cl, 5, text, true );
	}
	return true;
}
