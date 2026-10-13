/* Cry of Fear story co-op: cutscene watchdog. GPL-3.0-or-later.
 * The retail cutscenes already play for every player (sv_cof_scene_state.h);
 * this repairs the players they leave behind: a partner revived after a scene
 * still frozen in it, a respawn or rejoin mid-scene left free, a telescope
 * switched off under its user by the other player, and a late joiner left in
 * a model cutscene's black wait. It also skips scenes for the party (cof_skip).
 * Field offsets: retail CBasePlayer (0x281C). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_scene.h"
#include "sv_cof_scene_state.h"

#define COF_SCENE_TELESCOPE 0x370 // m_hTelescope EHANDLE (cof_telescope Use 1007D430)
#define COF_SCENE_CAMERA_MODEL 0x388 // EHANDLE to the player's camera_model (cof_mdlcutscene Use 100A4670)
#define COF_SCENE_CUTSCENE_WAIT 0x3C8 // byte: a late joiner waits out a model cutscene in black (100C948D, cleared 1009F584)
#define COF_SCENE_VIEW_INDEX 0x2038 // m_iViewEntIndex, kept by the retail SET_VIEW wrapper (10117180)
#define COF_SCENE_CAMERA 0x2348 // m_hCamera EHANDLE: InitHUD re-attaches a view through it (100F72A2)
#define COF_SCENE_HIDEHUD 0x2500 // m_iHideHUD: the wait hides bits 1|8 every PostThink
#define COF_SCENE_PLAYER_SIZE ( COF_SCENE_CAMERA + 8 )
#define COF_SCENE_MDL_SIZE 0x3F4 // cof_mdlcutscene
#define COF_SCENE_MDL_THINK 0x9F4A0 // RVA StartNextAnimation: set while it plays
#define COF_THINK 0x78 // CBaseEntity::m_pfnThink

typedef struct
{
	edict_t *edict;
	int serial;
} cof_scene_handle_t; // the retail EHANDLE on this 32-bit build

static struct
{
	int userid;
	int cutscene_wait;
} cof_scene_players[MAX_CLIENTS];

static convar_t *cof_skip_speed;
static cof_skip_t cof_skip;
static int cof_skip_generation = -1;

void SV_CoF_SceneReset( void )
{
	memset( cof_scene_players, 0, sizeof( cof_scene_players ));
	memset( &cof_skip, 0, sizeof( cof_skip ));
	cof_skip_generation = -1;
}

void SV_CoF_SceneInit( void )
{
	cof_skip_speed = Cvar_Get( "cof_skip_speed", "10", 0, "story co-op: how many times faster a skipped cutscene runs (cof_skip, Enter); below 2: no skipping" );
}

// A cof_mdlcutscene is playing: its think is StartNextAnimation and scheduled.
static qboolean CoF_SceneModelCutscene( void )
{
	uintptr_t base = SV_CoF_StoryBase();
	int i;
	if( !base ) return false;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		byte *pv;
		uint32_t think;
		if( !SV_IsValidEdict( ent ) || ( ent->v.flags & FL_KILLME ) || Q_strcmp( SV_GetString( ent->v.classname ), "cof_mdlcutscene" )) continue;
		if( !( pv = SV_CoF_StoryEntityPrivate( ent, COF_SCENE_MDL_SIZE ))) continue;
		memcpy( &think, pv + COF_THINK, sizeof( think ));
		if( think == base + COF_SCENE_MDL_THINK && ent->v.nextthink > 0.0f ) return true;
	}
	return false;
}

static qboolean CoF_SceneLiving( sv_client_t *cl )
{
	return cl->state == cs_spawned && !FBitSet( cl->flags, FCL_FAKECLIENT ) && SV_IsValidEdict( cl->edict ) &&
		cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
}

// The entity a retail EHANDLE names, if it still does.
static edict_t *CoF_SceneHandle( const byte *pv, int offset )
{
	cof_scene_handle_t h;
	memcpy( &h, pv + offset, sizeof( h ));
	if( !h.edict || h.edict < svgame.edicts || h.edict >= svgame.edicts + svgame.numEntities ) return NULL;
	if( !SV_IsValidEdict( h.edict ) || h.edict->serialnumber != h.serial ) return NULL;
	return h.edict;
}

// What this living player is going through, read from the engine and the
// verified player private data (NULL without it: then nothing is done).
static byte *CoF_SceneReadAny( sv_client_t *cl, cof_watch_t *w, qboolean dead_too )
{
	edict_t *ent = cl->edict, *view = cl->pViewEntity, *telescope;
	byte *pv;
	memset( w, 0, sizeof( *w ));
	if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( ent )) return NULL;
	if( !dead_too && !CoF_SceneLiving( cl )) return NULL;
	if( !( pv = SV_CoF_StoryEntityPrivate( ent, COF_SCENE_PLAYER_SIZE ))) return NULL;
	if( view && view != ent && view >= svgame.edicts && view < svgame.edicts + svgame.numEntities )
	{
		// A freed view entity is a camera that is over, whatever it was.
		if( !SV_IsValidEdict( view ) ) w->camera = NUM_FOR_EDICT( view );
		else if( !Q_strcmp( SV_GetString( view->v.classname ), "trigger_camera" ))
		{
			w->camera = NUM_FOR_EDICT( view );
			w->camera_live = view->v.iuser1 != 0 && !( view->v.flags & FL_KILLME );
			w->camera_freezes = ( view->v.spawnflags & 4 ) != 0;
		}
	}
	w->frozen = FBitSet( ent->v.flags, FL_FROZEN ) != 0;
	w->notarget = FBitSet( ent->v.flags, FL_NOTARGET ) != 0;
	w->invulnerable = ent->v.takedamage == DAMAGE_NO;
	telescope = CoF_SceneHandle( pv, COF_SCENE_TELESCOPE );
	w->telescope_off = telescope && !Q_strcmp( SV_GetString( telescope->v.classname ), "cof_telescope" ) && telescope->v.health == 0;
	w->cutscene_wait = pv[COF_SCENE_CUTSCENE_WAIT] != 0;
	w->camera_model = CoF_SceneHandle( pv, COF_SCENE_CAMERA_MODEL ) != NULL;
	w->animating = ent->v.fuser3 != 0.0f;
	return pv;
}

static byte *CoF_SceneRead( sv_client_t *cl, cof_watch_t *w )
{
	return CoF_SceneReadAny( cl, w, false );
}

// The retail camera end for one living player (1010B267): own view, control,
// targetable and vulnerable again, and no handle for InitHUD to re-attach.
static void CoF_SceneRelease( sv_client_t *cl, byte *pv )
{
	edict_t *ent = cl->edict;
	int self = NUM_FOR_EDICT( ent );
	cl->pViewEntity = NULL;
	MSG_BeginServerCmd( &cl->netchan.message, svc_setview );
	MSG_WriteWord( &cl->netchan.message, self );
	memcpy( pv + COF_SCENE_VIEW_INDEX, &self, sizeof( self ));
	memset( pv + COF_SCENE_CAMERA, 0, sizeof( cof_scene_handle_t ));
	ClearBits( ent->v.flags, FL_FROZEN | FL_NOTARGET );
	// A corpse keeps whatever its death set; the next spawn resets it.
	if( ent->v.health > 0 && ent->v.deadflag == DEAD_NO ) ent->v.takedamage = DAMAGE_AIM;
}

// What cof_mdlcutscene's end does for a waiting player (1009F584), for one
// whose wait no model cutscene will end.
static void CoF_SceneUnwait( sv_client_t *cl, byte *pv )
{
	int hud;
	pv[COF_SCENE_CUTSCENE_WAIT] = 0;
	ClearBits( cl->edict->v.flags, FL_FROZEN | FL_NOTARGET );
	memcpy( &hud, pv + COF_SCENE_HIDEHUD, sizeof( hud ));
	hud &= ~9;
	memcpy( pv + COF_SCENE_HIDEHUD, &hud, sizeof( hud ));
	SV_CoF_RetailScreenFade( cl, 1.0f, 0.0f, 0, 0, 0, 0, 255 );
}

// What the retail camera start does to every player (1011420B), for one it missed.
static void CoF_SceneHold( sv_client_t *cl )
{
	edict_t *ent = cl->edict;
	if( !FBitSet( ent->v.flags, FL_FROZEN )) VectorClear( ent->v.velocity );
	SetBits( ent->v.flags, FL_FROZEN | FL_NOTARGET );
	ent->v.takedamage = DAMAGE_NO;
}

// The other player switched this one's telescope off (cof_telescope keeps one
// user handle): turn it back on and let the game's own cancel take this player
// out of it (telescopecancel 1001C150 -> Use: unfreeze, scope and zoom off).
static void CoF_SceneTelescope( sv_client_t *cl, byte *pv )
{
	edict_t *telescope = CoF_SceneHandle( pv, COF_SCENE_TELESCOPE );
	if( !telescope || !svgame.dllFuncs.pfnClientCommand ) return;
	telescope->v.health = 1.0f;
	Cmd_TokenizeString( "telescopecancel" );
	SV_CoF_CommandSender( cl->edict );
	svgame.dllFuncs.pfnClientCommand( cl->edict );
	SV_CoF_CommandSender( NULL );
}

void SV_CoF_SceneFrame( void )
{
	int i, model_cutscene = -1;
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_watch_t w;
		byte *pv = CoF_SceneRead( cl, &w );
		int act;
		if( cof_scene_players[i].userid != cl->userid )
		{
			cof_scene_players[i].userid = cl->userid;
			cof_scene_players[i].cutscene_wait = 0;
		}
		if( !pv ) continue;
		w.was_cutscene_wait = cof_scene_players[i].cutscene_wait;
		cof_scene_players[i].cutscene_wait = w.cutscene_wait;
		if( w.cutscene_wait )
		{
			if( model_cutscene < 0 ) model_cutscene = CoF_SceneModelCutscene();
			w.model_cutscene = model_cutscene;
		}
		act = CoF_WatchDecide( &w );
		if( act & COF_WATCH_RELEASE )
		{
			CoF_SceneRelease( cl, pv );
			Con_Printf( "cofstory phase=camera-released userid=%d camera=%d\n", cl->userid, w.camera );
		}
		if( act & COF_WATCH_HOLD )
		{
			CoF_SceneHold( cl );
			Con_Printf( "cofstory phase=camera-held userid=%d camera=%d\n", cl->userid, w.camera );
		}
		if( act & COF_WATCH_TELESCOPE )
		{
			CoF_SceneTelescope( cl, pv );
			Con_Printf( "cofstory phase=telescope-released userid=%d\n", cl->userid );
		}
		if( act & COF_WATCH_FADE_IN )
		{
			SV_CoF_RetailScreenFade( cl, 1.0f, 0.0f, 0, 0, 0, 0, 255 );
			Con_Printf( "cofstory phase=cutscene-wait-over userid=%d\n", cl->userid );
		}
		if( act & COF_WATCH_UNWAIT )
		{
			CoF_SceneUnwait( cl, pv );
			cof_scene_players[i].cutscene_wait = 0;
			Con_Printf( "cofstory phase=cutscene-wait-released userid=%d\n", cl->userid );
		}
	}
}

void SV_CoF_SceneSettle( const char *reason )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() ) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_watch_t w;
		// The dead too: a CPR revive in the next map would bring the freeze back.
		byte *pv = CoF_SceneReadAny( cl, &w, true );
		if( !pv || !( CoF_WatchSettle( &w ) & COF_WATCH_RELEASE )) continue;
		CoF_SceneRelease( cl, pv );
		Con_Printf( "cofstory phase=camera-released userid=%d camera=%d reason=%s\n", cl->userid, w.camera, reason );
	}
}

qboolean SV_CoF_SceneActive( void )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() ) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		cof_watch_t w;
		if( CoF_SceneRead( &svs.clients[i], &w ) && w.camera_live ) return true;
	}
	return false;
}

// A skip belongs to the map it was asked on.
static void CoF_SceneSkipMap( void )
{
	if( cof_skip_generation == svs.spawncount ) return;
	memset( &cof_skip, 0, sizeof( cof_skip ));
	cof_skip_generation = svs.spawncount;
}

// Every living person is held by a scene, and there is one: the world may run faster.
static qboolean CoF_SceneAllHeld( void )
{
	int i, living = 0, model_cutscene = -1;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_watch_t w;
		if( !CoF_SceneRead( cl, &w ))
		{
			if( CoF_SceneLiving( cl )) return false; // not known to be held
			continue;
		}
		living++;
		if( w.frozen && !( w.camera && w.camera_live ))
		{
			if( model_cutscene < 0 ) model_cutscene = CoF_SceneModelCutscene();
			w.model_cutscene = model_cutscene;
		}
		if( !CoF_SkipHeld( &w )) return false;
	}
	return living > 0;
}

int SV_CoF_SceneSkipSteps( void )
{
	int over, steps;
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return 1;
	CoF_SceneSkipMap();
	if( !cof_skip.wanted ) return 1;
	steps = CoF_SkipSteps( &cof_skip, CoF_SceneAllHeld(), sv.time, cof_skip_speed ? (int)cof_skip_speed->value : 1, &over );
	if( over ) Con_Printf( "cofstory phase=cutscene-skip-over\n" );
	return steps;
}

qboolean SV_CoF_SceneClientCommand( sv_client_t *cl )
{
	char text[96];
	int i;
	if( Q_stricmp( Cmd_Argv( 0 ), "cof_skip" ) || !SV_CoF_StoryCoopEnabled() ) return false;
	if( sv.state != ss_active || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT )) return true;
	CoF_SceneSkipMap();
	if( !cof_skip_speed || cof_skip_speed->value < 2.0f )
	{
		SV_CoF_RetailProFont( cl, 5, "Cutscene skipping is off (cof_skip_speed).", true );
		return true;
	}
	if( Cvar_VariableValue( "sv_fps" ) != 0.0f )
	{
		SV_CoF_RetailProFont( cl, 5, "Cutscene skipping needs sv_fps 0.", true );
		return true;
	}
	if( cof_skip.wanted )
	{
		memset( &cof_skip, 0, sizeof( cof_skip ));
		Q_snprintf( text, sizeof( text ), "%s is watching the cutscene again.", cl->name );
		Con_Printf( "cofstory phase=cutscene-skip-stopped userid=%d\n", cl->userid );
	}
	else if( CoF_SceneAllHeld())
	{
		cof_skip.wanted = cl->userid;
		Q_snprintf( text, sizeof( text ), "%s skips the cutscene (Enter: watch it).", cl->name );
		Con_Printf( "cofstory phase=cutscene-skip userid=%d\n", cl->userid );
	}
	else
	{
		SV_CoF_RetailProFont( cl, 5, "No cutscene to skip right now.", true );
		return true;
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && !FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ))
			SV_CoF_RetailProFont( &svs.clients[i], 4, text, true );
	return true;
}

qboolean SV_CoF_SceneWatching( int owner_userid, int userid )
{
	cof_watch_t a, b;
	sv_client_t *owner = NULL, *other = NULL;
	int i;
	if( !SV_CoF_StoryCoopEnabled() || !owner_userid || !userid || owner_userid == userid ) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		if( svs.clients[i].userid == owner_userid ) owner = &svs.clients[i];
		if( svs.clients[i].userid == userid ) other = &svs.clients[i];
	}
	return owner && other && CoF_SceneRead( owner, &a ) && CoF_SceneRead( other, &b ) && CoF_WatchTogether( &a, &b );
}
