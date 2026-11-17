/*
sv_cof_light.c - Cry of Fear story co-op: the co-op flashlight is the host's choice (sv_cof_light.h)
GPL-3.0-or-later.

Off (the default): a player's "flashlight" command never reaches the game, a torch already on is switched off, and
the AI companion does not light one (sv_cof_companion.c). Logs: phase=flashlight-refused, phase=flashlight-off.

Night vision: the game's F switches it only in single player, so in co-op a player who picked up the bonus room's
item_nightvision could never use it. With cof_coop_nightvision (default 1) F is that player's night vision, as in
single player, before the torch rule. Log: phase=nightvision.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_light.h"

static CVAR_DEFINE_AUTO( cof_coop_flashlight, "0", 0, "Cry of Fear story co-op: the co-op flashlight (F, a free torch everyone sees) is allowed; 0: only the story's own lights, as in single player" );

static CVAR_DEFINE_AUTO( cof_coop_nightvision, "1", 0, "Cry of Fear story co-op: F switches the night vision of a player who owns it (bonus room), as in single player; 0: the game's multiplayer F" );

static double cof_light_next_try[MAX_CLIENTS];
static int cof_light_generation = -1;

void SV_CoF_LightInit( void )
{
	Cvar_RegisterVariable( &cof_coop_flashlight );
	Cvar_RegisterVariable( &cof_coop_nightvision );
}

qboolean SV_CoF_CoopFlashlightAllowed( void )
{
	return cof_coop_flashlight.value != 0.0f;
}

qboolean SV_CoF_LightClientCommand( sv_client_t *cl )
{
	int night;
	if( SV_CoF_StoryCoopEnabled() && cof_coop_nightvision.value != 0.0f && !Q_strcmp( Cmd_Argv( 0 ), "flashlight" ) &&
		( night = SV_CoF_RetailNightVision( cl )) >= 0 )
	{
		Con_Printf( "cofstory phase=nightvision userid=%d switched=%d\n", cl->userid, night );
		return true;
	}
	if( !CoF_LightRefused( Cmd_Argv( 0 ), SV_CoF_StoryCoopEnabled(), SV_CoF_CoopFlashlightAllowed( )))
		return false;
	Con_Printf( "cofstory phase=flashlight-refused userid=%d\n", cl->userid );
	return true;
}

void SV_CoF_LightFrame( void )
{
	qboolean allowed = SV_CoF_CoopFlashlightAllowed();
	int i;
	if( cof_light_generation != svs.spawncount ) // sv.time starts again with each map
	{
		cof_light_generation = svs.spawncount;
		memset( cof_light_next_try, 0, sizeof( cof_light_next_try ));
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		if( !CoF_LightSwitchOff( cl->edict->v.frags, allowed, sv.time, cof_light_next_try[i] )) continue;
		cof_light_next_try[i] = sv.time + COF_LIGHT_RETRY;
		if( SV_CoF_RetailCommand( cl, "flashlight" ))
			Con_Printf( "cofstory phase=flashlight-off userid=%d frags=%d\n", cl->userid, (int)cl->edict->v.frags );
	}
}
