/* Cry of Fear story co-op: cutscene watchdog. GPL-3.0-or-later.
 * The retail cutscenes already play for every player (sv_cof_scene_state.h);
 * this repairs the players they leave behind: a partner revived after a scene
 * still frozen in it, a respawn or rejoin mid-scene left free, a telescope
 * switched off under its user by the other player, and a late joiner left in
 * a model cutscene's black wait. It also skips scenes for the party (cof_skip).
 * 1.1.8: the wardrobe's camera is its user's alone and is closed when that user
 * is gone (K1); in a merged map a scene of one section holds nobody in another
 * (K4); and two players who watched one camera to its end, or are frozen in one
 * model cutscene, still count as watching together (M1 finding 5, M3 finding 2).
 * Field offsets: retail CBasePlayer (0x281C). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_sections.h"
#include "sv_cof_scene.h"
#include "sv_cof_scene_state.h"

#define COF_SCENE_TELESCOPE 0x370 // m_hTelescope EHANDLE (cof_telescope Use 1007D430)
#define COF_SCENE_CAMERA_MODEL 0x388 // EHANDLE to the player's camera_model (cof_mdlcutscene Use 100A4670)
#define COF_SCENE_CUTSCENE_WAIT 0x3C8 // byte: a late joiner waits out a model cutscene in black (100C948D, cleared 1009F584)
#define COF_SCENE_DEATH_SCENE 0x5AC // raw pDeathScene: the same camera model, walked by the model cutscene's think
#define COF_SCENE_BARS 0x1EB4 // m_DrawBars: a camera's spawnflag 32, a model cutscene's 4
#define COF_SCENE_VIEW_INDEX 0x2038 // m_iViewEntIndex, kept by the retail SET_VIEW wrapper (10117180)
#define COF_SCENE_CAMERA 0x2348 // m_hCamera EHANDLE: InitHUD re-attaches a view through it (100F72A2)
#define COF_SCENE_HIDEHUD 0x2500 // m_iHideHUD: the wait hides bits 1|8 every PostThink
#define COF_SCENE_PLAYER_SIZE ( COF_SCENE_CAMERA + 8 )
#define COF_SCENE_MDL_SIZE 0x3F4 // cof_mdlcutscene
#define COF_SCENE_MDL_THINK 0x9F4A0 // RVA StartNextAnimation: set while it plays
#define COF_SCENE_CAMERA_USER 0xEC // trigger_camera m_hPlayer EHANDLE, its first member (CBaseDelay ends at 0xEC)
#define COF_SCENE_CAMERA_SIZE ( COF_SCENE_CAMERA_USER + 8 )
#define COF_SCENE_CLOTHES_CAMERA 0xDC // cof_clothesmenu m_sCameraEnt string_t, its first member (CBaseEntity ends at 0xDC)
#define COF_SCENE_CLOTHES_SIZE 0xE4
#define COF_SCENE_WARDROBES 4
#define COF_SCENE_CLOSE_RETRY 5.0 // seconds between two closes of one wardrobe camera the game left running
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
	cof_watch_seen_t seen; // the camera they last watched running (1.1.8)
} cof_scene_players[MAX_CLIENTS];

static convar_t *cof_skip_speed;
static cof_skip_t cof_skip;
static int cof_skip_generation = -1;
// The map's wardrobe cameras (a cof_clothesmenu's cameraent), found once per map.
static struct { int index, serial; } cof_scene_wardrobes[COF_SCENE_WARDROBES];
static int cof_scene_wardrobe_count, cof_scene_wardrobe_generation = -1;
static int cof_scene_seen_generation = -1; // the map the seen records belong to (its time starts again)
static double cof_scene_close_tried;

void SV_CoF_SceneReset( void )
{
	memset( cof_scene_players, 0, sizeof( cof_scene_players ));
	memset( &cof_skip, 0, sizeof( cof_skip ));
	cof_skip_generation = -1;
	memset( cof_scene_wardrobes, 0, sizeof( cof_scene_wardrobes ));
	cof_scene_wardrobe_count = 0;
	cof_scene_wardrobe_generation = -1;
	cof_scene_close_tried = 0.0;
	cof_scene_seen_generation = -1;
}

void SV_CoF_SceneInit( void )
{
	cof_skip_speed = Cvar_Get( "cof_skip_speed", "10", 0, "story co-op: how many times faster a skipped cutscene runs (cof_skip, Enter); below 2: no skipping" );
}

// This cof_mdlcutscene is playing: its think is StartNextAnimation and scheduled.
static qboolean CoF_SceneModelRunning( edict_t *ent, uintptr_t base )
{
	byte *pv;
	uint32_t think;
	if( !SV_IsValidEdict( ent ) || ( ent->v.flags & FL_KILLME ) || Q_strcmp( SV_GetString( ent->v.classname ), "cof_mdlcutscene" )) return false;
	if( !( pv = SV_CoF_StoryEntityPrivate( ent, COF_SCENE_MDL_SIZE ))) return false;
	memcpy( &think, pv + COF_THINK, sizeof( think ));
	return think == base + COF_SCENE_MDL_THINK && ent->v.nextthink > 0.0f;
}

// A cof_mdlcutscene is playing; in a merged map, one in this section (section -1: anywhere).
static qboolean CoF_SceneModelCutscene( int section )
{
	uintptr_t base = SV_CoF_StoryBase();
	int i;
	if( !base ) return false;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( CoF_SceneModelRunning( ent, base ) && ( section < 0 || SV_CoF_PointSection( ent->v.origin ) == section )) return true;
	}
	return false;
}

// The playing cof_mdlcutscene a camera model belongs to: the cutscene creates each player's model at its own
// origin (100A4670), and a camera model never moves.
static edict_t *CoF_SceneModelOf( const edict_t *model )
{
	uintptr_t base = SV_CoF_StoryBase();
	int i;
	if( !base || !model ) return NULL;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( fabs( ent->v.origin[0] - model->v.origin[0] ) > 1.0f || fabs( ent->v.origin[1] - model->v.origin[1] ) > 1.0f ||
			fabs( ent->v.origin[2] - model->v.origin[2] ) > 1.0f ) continue;
		if( CoF_SceneModelRunning( ent, base )) return ent;
	}
	return NULL;
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

// 1.1.8 (K1, K4): out of a running camera that is not theirs, with the HUD and bars its end would give back
// (FollowTarget 1010AF30); the game tells their client itself (UpdateClientData).
static void CoF_SceneReleaseElsewhere( sv_client_t *cl, byte *pv )
{
	const edict_t *camera = cl->pViewEntity;
	int hud, bars = 0;
	if( camera )
	{
		memcpy( &hud, pv + COF_SCENE_HIDEHUD, sizeof( hud ));
		if( camera->v.spawnflags & 16 ) hud &= ~8;
		if( camera->v.iuser2 ) hud &= ~16;
		memcpy( pv + COF_SCENE_HIDEHUD, &hud, sizeof( hud ));
		if( camera->v.spawnflags & 32 ) memcpy( pv + COF_SCENE_BARS, &bars, sizeof( bars ));
	}
	CoF_SceneRelease( cl, pv );
}

// 1.1.8 (K4): what cof_mdlcutscene's end (1009F584) does for each player, for one held by a model cutscene of
// another merged-map section: their camera model let go and removed (the cutscene's think skips a player without
// one), control, targetability, HUD and bars back. Never its owner: the cutscene stops for good without the
// owner's model (StartNextAnimation 1009F4A0). The game tells their client itself (UpdateClientData).
static void CoF_SceneModelRelease( sv_client_t *cl, byte *pv, const edict_t *cutscene )
{
	edict_t *ent = cl->edict;
	int flags = cutscene->v.spawnflags, hud, bars = 0, i;
	uint32_t none = 0;
	memcpy( pv + COF_SCENE_DEATH_SCENE, &none, sizeof( none ));
	memset( pv + COF_SCENE_CAMERA_MODEL, 0, sizeof( cof_scene_handle_t ));
	pv[COF_SCENE_CUTSCENE_WAIT] = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *model = SV_EdictNum( i );
		if( SV_IsValidEdict( model ) && model->v.owner == ent && !Q_strcmp( SV_GetString( model->v.classname ), "camera_model" ))
			SetBits( model->v.flags, FL_KILLME );
	}
	memcpy( &hud, pv + COF_SCENE_HIDEHUD, sizeof( hud ));
	hud &= ~1;
	if( flags & 2 ) hud &= ~8;
	if( flags & 64 ) hud &= ~16;
	memcpy( pv + COF_SCENE_HIDEHUD, &hud, sizeof( hud ));
	if( flags & 4 ) memcpy( pv + COF_SCENE_BARS, &bars, sizeof( bars ));
	if( flags & 1 ) ClearBits( ent->v.flags, FL_FROZEN );
	ClearBits( ent->v.flags, FL_NOTARGET );
}

// The map's wardrobe cameras: the trigger_camera each cof_clothesmenu turns on (its cameraent). Its start loop
// views, freezes and blinds every player (1011420B), but only its user gets the menu, and its wait 666 never times
// out: only the user's closeclothes ends it (1.1.8, K1).
static void CoF_SceneWardrobes( void )
{
	int i, j;
	if( cof_scene_wardrobe_generation == svs.spawncount ) return;
	cof_scene_wardrobe_generation = svs.spawncount;
	cof_scene_wardrobe_count = 0;
	cof_scene_close_tried = 0.0;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *menu = SV_EdictNum( i );
		const char *name = NULL;
		string_t camera;
		byte *pv;
		if( !SV_IsValidEdict( menu ) || Q_strcmp( SV_GetString( menu->v.classname ), "cof_clothesmenu" )) continue;
		if( !( pv = SV_CoF_StoryEntityPrivate( menu, COF_SCENE_CLOTHES_SIZE ))) continue;
		memcpy( &camera, pv + COF_SCENE_CLOTHES_CAMERA, sizeof( camera ));
		if( !camera || !( name = SV_GetString( camera ))[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities && cof_scene_wardrobe_count < COF_SCENE_WARDROBES; j++ )
		{
			edict_t *ent = SV_EdictNum( j );
			if( !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.classname ), "trigger_camera" ) ||
				Q_strcmp( SV_GetString( ent->v.targetname ), name )) continue;
			cof_scene_wardrobes[cof_scene_wardrobe_count].index = j;
			cof_scene_wardrobes[cof_scene_wardrobe_count].serial = ent->serialnumber;
			cof_scene_wardrobe_count++;
			Con_Printf( "cofstory phase=wardrobe-camera camera=%d menu=%d\n", j, i );
		}
	}
}

// A wardrobe camera, and the player it is for (*user: its m_hPlayer, NULL when that handle is stale).
static qboolean CoF_SceneWardrobeUser( edict_t *camera, edict_t **user )
{
	int i, index = NUM_FOR_EDICT( camera );
	byte *pv;
	*user = NULL;
	for( i = 0; i < cof_scene_wardrobe_count; i++ )
		if( cof_scene_wardrobes[i].index == index && cof_scene_wardrobes[i].serial == camera->serialnumber ) break;
	if( i == cof_scene_wardrobe_count ) return false;
	if(( pv = SV_CoF_StoryEntityPrivate( camera, COF_SCENE_CAMERA_SIZE )) != NULL ) *user = CoF_SceneHandle( pv, COF_SCENE_CAMERA_USER );
	return true;
}

// The living, spawned client this edict is, or NULL.
static sv_client_t *CoF_SceneLivingClient( const edict_t *ent )
{
	int index = ent ? NUM_FOR_EDICT( ent ) : 0;
	sv_client_t *cl;
	if( index < 1 || index > svs.maxclients || index > MAX_CLIENTS ) return NULL;
	cl = &svs.clients[index - 1];
	return cl->state == cs_spawned && cl->edict == ent && ent->v.health > 0 && ent->v.deadflag == DEAD_NO ? cl : NULL;
}

// 1.1.8 (K1, M1 finding 3): a wardrobe whose user left or died is closed by the game's own closeclothes command
// (the first cof_clothesmenu, Use OFF 1 -> the camera's Use OFF 666, which frees every living player), sent as that
// user while still connected, else as another person.
static void CoF_SceneWardrobeClose( void )
{
	int i, j;
	for( i = 0; i < cof_scene_wardrobe_count; i++ )
	{
		edict_t *camera = SV_EdictNum( cof_scene_wardrobes[i].index ), *user = NULL;
		sv_client_t *closer = NULL;
		if( !SV_IsValidEdict( camera ) || camera->serialnumber != cof_scene_wardrobes[i].serial || !camera->v.iuser1 ||
			( camera->v.flags & FL_KILLME ) || !CoF_SceneWardrobeUser( camera, &user ) || CoF_SceneLivingClient( user )) continue;
		if( cof_scene_close_tried && sv.time >= cof_scene_close_tried && sv.time < cof_scene_close_tried + COF_SCENE_CLOSE_RETRY ) continue;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *cl = &svs.clients[j];
			if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
			if( !closer || cl->edict == user ) closer = cl;
		}
		if( !closer || !svgame.dllFuncs.pfnClientCommand ) continue;
		cof_scene_close_tried = sv.time;
		Cmd_TokenizeString( "closeclothes" );
		SV_CoF_CommandSender( closer->edict );
		SV_CoF_RetailGameCommand( closer );
		SV_CoF_CommandSender( NULL );
		Con_Printf( "cofstory phase=wardrobe-closed camera=%d sender=%d reason=%s\n", cof_scene_wardrobes[i].index, closer->userid,
			closer->edict == user ? "user-dead" : "user-gone" );
	}
}

// 1.1.8 (K1, K4): whose scenes these are. A wardrobe's camera is its user's alone; in a merged map a scene of
// another section is another original level's. models: also find the model cutscene their camera model belongs to
// (a search of the edicts: only where it is needed).
static void CoF_SceneWhere( sv_client_t *cl, cof_watch_t *w, byte *pv, qboolean models )
{
	edict_t *ent = cl->edict, *view = cl->pViewEntity, *user = NULL, *cutscene = NULL;
	int section = SV_CoF_PlayerSection( ent );
	if( w->camera && w->camera_live )
	{
		if( CoF_SceneWardrobeUser( view, &user )) w->camera_elsewhere = user != ent;
		else if( section >= 0 && SV_CoF_PointSection( view->v.origin ) != section ) w->camera_elsewhere = 1;
	}
	if( !models || !w->camera_model || !( cutscene = CoF_SceneModelOf( CoF_SceneHandle( pv, COF_SCENE_CAMERA_MODEL )))) return;
	w->model_scene = NUM_FOR_EDICT( cutscene );
	w->model_elsewhere = section >= 0 && cutscene->v.owner != ent && SV_CoF_PointSection( cutscene->v.origin ) != section;
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
	SV_CoF_RetailGameCommand( cl );
	SV_CoF_CommandSender( NULL );
}

void SV_CoF_SceneFrame( void )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	CoF_SceneWardrobes();
	CoF_SceneWardrobeClose();
	if( cof_scene_seen_generation != svs.spawncount )
	{
		cof_scene_seen_generation = svs.spawncount;
		for( i = 0; i < MAX_CLIENTS; i++ ) memset( &cof_scene_players[i].seen, 0, sizeof( cof_scene_players[i].seen ));
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_watch_t w;
		byte *pv = CoF_SceneRead( cl, &w );
		int act, section;
		if( cof_scene_players[i].userid != cl->userid )
		{
			cof_scene_players[i].userid = cl->userid;
			cof_scene_players[i].cutscene_wait = 0;
			memset( &cof_scene_players[i].seen, 0, sizeof( cof_scene_players[i].seen ));
		}
		if( !pv ) continue;
		section = SV_CoF_PlayerSection( cl->edict );
		CoF_SceneWhere( cl, &w, pv, section >= 0 );
		if( w.camera && w.camera_live && !w.camera_elsewhere )
		{
			cof_scene_players[i].seen.camera = w.camera;
			cof_scene_players[i].seen.serial = SV_EdictNum( w.camera )->serialnumber;
			cof_scene_players[i].seen.seen = sv.time;
		}
		w.was_cutscene_wait = cof_scene_players[i].cutscene_wait;
		cof_scene_players[i].cutscene_wait = w.cutscene_wait;
		if( w.cutscene_wait ) w.model_cutscene = CoF_SceneModelCutscene( section );
		act = CoF_WatchDecide( &w );
		if(( act & COF_WATCH_RELEASE ) && w.camera_elsewhere )
		{
			memset( &cof_scene_players[i].seen, 0, sizeof( cof_scene_players[i].seen ));
			CoF_SceneReleaseElsewhere( cl, pv );
			Con_Printf( "cofstory phase=camera-released userid=%d camera=%d reason=%s\n", cl->userid, w.camera, section >= 0 &&
				SV_CoF_PointSection( SV_EdictNum( w.camera )->v.origin ) != section ? "section" : "wardrobe" );
		}
		else if( act & COF_WATCH_RELEASE )
		{
			CoF_SceneRelease( cl, pv );
			Con_Printf( "cofstory phase=camera-released userid=%d camera=%d\n", cl->userid, w.camera );
		}
		if( act & COF_WATCH_MODEL_RELEASE )
		{
			CoF_SceneModelRelease( cl, pv, SV_EdictNum( w.model_scene ));
			Con_Printf( "cofstory phase=cutscene-released userid=%d cutscene=%d reason=section\n", cl->userid, w.model_scene );
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
			if( model_cutscene < 0 ) model_cutscene = CoF_SceneModelCutscene( -1 );
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

// The camera this record names is still running.
static qboolean CoF_SceneSeenRunning( const cof_watch_seen_t *seen )
{
	edict_t *camera;
	if( !seen->camera || seen->camera >= svgame.numEntities ) return false;
	camera = SV_EdictNum( seen->camera );
	return SV_IsValidEdict( camera ) && camera->serialnumber == seen->serial && camera->v.iuser1 != 0 &&
		!( camera->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( camera->v.classname ), "trigger_camera" );
}

qboolean SV_CoF_SceneWatching( int owner_userid, int userid )
{
	cof_watch_t a, b;
	sv_client_t *owner = NULL, *other = NULL;
	byte *pa = NULL, *pb = NULL;
	const cof_watch_seen_t *sa, *sb;
	int i;
	if( !SV_CoF_StoryCoopEnabled() || !owner_userid || !userid || owner_userid == userid ) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		if( svs.clients[i].userid == owner_userid ) owner = &svs.clients[i];
		if( svs.clients[i].userid == userid ) other = &svs.clients[i];
	}
	if( !owner || !other || !( pa = CoF_SceneRead( owner, &a )) || !( pb = CoF_SceneRead( other, &b ))) return false;
	CoF_SceneWhere( owner, &a, pa, a.frozen && b.frozen );
	CoF_SceneWhere( other, &b, pb, a.frozen && b.frozen );
	if( CoF_WatchTogether( &a, &b )) return true;
	// 1.1.8 (M1 finding 5): or both watched one camera to its end just now (a scene's closing teleport)
	sa = &cof_scene_players[owner - svs.clients].seen;
	sb = &cof_scene_players[other - svs.clients].seen;
	if( cof_scene_players[owner - svs.clients].userid != owner_userid || cof_scene_players[other - svs.clients].userid != userid ||
		cof_scene_seen_generation != svs.spawncount ) return false;
	return CoF_WatchRecent( sa, sb, CoF_SceneSeenRunning( sa ), sv.time );
}
