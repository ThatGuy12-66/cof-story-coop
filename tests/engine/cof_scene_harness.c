/* The cutscene watchdog (sv_cof_scene.c) against engine doubles: no game DLL,
 * networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp /* cc on Linux: the same compare MSVC names _stricmp */
#endif
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_zombie 1
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define DAMAGE_NO 0
#define DAMAGE_AIM 2
#define FL_NOTARGET (1<<7)
#define FL_FROZEN (1<<12)
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define svc_setview 5
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
#define Q_snprintf snprintf
#define FBitSet(f,b) ((f)&(b))
#define SetBits(f,b) ((f)|=(b))
#define ClearBits(f,b) ((f)&=~(b))
#define VectorClear(a) memset((a),0,sizeof(vec3_t))
typedef struct edict_s edict_t;
typedef struct { string_t classname,targetname; vec3_t origin,velocity; float health,takedamage,nextthink,fuser3; int deadflag,flags,spawnflags,iuser1,iuser2; struct edict_s *owner; } entvars_t;
struct edict_s { int free,serialnumber; void *pvPrivateData; entvars_t v; };
typedef struct { int last_cmd, last_word, count; } sizebuf_t;
typedef struct { sizebuf_t message; } netchan_t;
typedef struct { int state,userid; unsigned int flags; char name[32]; edict_t *edict,*pViewEntity; netchan_t netchan; } sv_client_t;
typedef struct { float value; const char *string; } convar_t;
static struct { int state; double time; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { edict_t *edicts; int numEntities; struct { void (*pfnClientCommand)( edict_t * ); } dllFuncs; } svgame;
static edict_t entities[16];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[MAX_CLIENTS][0x2400];
static const char *strings[] = { "", "player", "trigger_camera", "cof_telescope", "info_target", "cof_mdlcutscene", "camera_model",
	"cof_clothesmenu", "clothescamera" };
static byte mdl_private[0x400], entity_private[16][0x400];
#define BASE 0x10000000u
static char log_text[16384];
static size_t log_length;
static int gate, fades, commands, sender_during;
static char last_command[64];
static edict_t *sender;

/* A merged map's sections stacked by height: section 1 from z 1000 up (merged), else not merged (-1). */
static int merged;
static int SV_CoF_PointSection( const vec3_t origin ) { return merged ? origin[2] >= 1000.0f : -1; }
static int SV_CoF_PlayerSection( const edict_t *ent ) { return SV_CoF_PointSection( ent->v.origin ); }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static uintptr_t SV_CoF_StoryBase( void ) { return gate ? BASE : 0; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + 16 && !e->free; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return SV_IsValidEdict( e ) && need <= 0x2400 ? e->pvPrivateData : NULL; }
static void MSG_BeginServerCmd( sizebuf_t *msg, int cmd ) { msg->last_cmd = cmd; msg->count++; }
static void MSG_WriteWord( sizebuf_t *msg, int w ) { msg->last_word = w; }
static qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a )
{
	assert( duration == 1.0f && hold == 0.0f && flags == 0 && !r && !g && !b && a == 255 );
	fades++; return true;
}
static void Cmd_TokenizeString( const char *text ) { strncpy( last_command, text, sizeof( last_command ) - 1 ); }
static convar_t skip_speed = { 10, "10" };
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *description )
{
	assert( !strcmp( name, "cof_skip_speed" ) && !strcmp( value, "10" ));
	return &skip_speed;
}
static float sv_fps_value;
static float Cvar_VariableValue( const char *name ) { assert( !strcmp( name, "sv_fps" )); return sv_fps_value; }
static const char *argv0 = "";
static const char *Cmd_Argv( int i ) { return i == 0 ? argv0 : ""; }
static char told[MAX_CLIENTS][96];
static int told_count;
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	snprintf( told[cl - clients], sizeof( told[0] ), "%s", text ); told_count++; return true;
}
static void SV_CoF_CommandSender( edict_t *e ) { sender = e; }
static edict_t *command_sender;
static void SV_CoF_RetailGameCommand( sv_client_t *cl ) { svgame.dllFuncs.pfnClientCommand( cl->edict ); }
static void Con_Printf( const char *fmt, ... )
{
	va_list args; int n;
	va_start( args, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, args );
	va_end( args );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}

#include "scene_under_test.c"

enum { CAM = 9, TELESCOPE = 10, MDL = 11, MENU = 13 };
static int wardrobe_closes;

/* The game's telescopecancel: if the sender's handle names a telescope that is
 * on, its Use switches it off for the sender and clears the handle. */
static void game_command( edict_t *player )
{
	byte *pv = player->pvPrivateData;
	cof_scene_handle_t h;
	commands++;
	sender_during = sender == player;
	command_sender = sender;
	/* closeclothes: the wardrobe's camera off (Use OFF 666) */
	if( !strcmp( last_command, "closeclothes" ) && wardrobe_closes ) entities[CAM].v.iuser1 = 0;
	memcpy( &h, pv + COF_SCENE_TELESCOPE, sizeof( h ));
	if( !strcmp( last_command, "telescopecancel" ) && h.edict && h.edict->v.health != 0 )
	{
		h.edict->v.health = 0;
		memset( pv + COF_SCENE_TELESCOPE, 0, sizeof( h ));
		player->v.flags &= ~FL_FROZEN;
	}
}

static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities )); memset( clients, 0, sizeof( clients ));
	memset( private_data, 0, sizeof( private_data ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	gate = 1; fades = commands = sender_during = 0; sender = command_sender = NULL; last_command[0] = 0; merged = 0; wardrobe_closes = 1;
	memset( entity_private, 0, sizeof( entity_private ));
	for( i = 3; i < 16; i++ ) entities[i].pvPrivateData = entity_private[i];
	sv.state = ss_active; sv.time = 100.0; svs.maxclients = 2; svs.spawncount = 1; svs.clients = clients;
	memset( told, 0, sizeof( told )); told_count = 0; skip_speed.value = 10; argv0 = ""; sv_fps_value = 0;
	svgame.edicts = entities; svgame.numEntities = 16; svgame.dllFuncs.pfnClientCommand = game_command;
	SV_CoF_SceneReset();
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned; clients[i].userid = 100 + i; clients[i].edict = &entities[i + 1];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		entities[i + 1].pvPrivateData = private_data[i]; entities[i + 1].v.classname = 1;
		entities[i + 1].v.health = 100; entities[i + 1].v.takedamage = DAMAGE_AIM;
		entities[i + 1].v.velocity[0] = 50;
	}
	entities[CAM].v.classname = 2; entities[CAM].v.spawnflags = 116; entities[CAM].v.iuser1 = 1; entities[CAM].serialnumber = 3;
	entities[TELESCOPE].v.classname = 3; entities[TELESCOPE].serialnumber = 5;
	memset( mdl_private, 0, sizeof( mdl_private ));
}
/* A cof_mdlcutscene playing: StartNextAnimation scheduled. */
static void model_cutscene( int playing )
{
	uint32_t think = playing ? BASE + COF_SCENE_MDL_THINK : 0;
	entities[MDL].v.classname = 5; entities[MDL].pvPrivateData = mdl_private;
	memcpy( mdl_private + COF_THINK, &think, sizeof( think ));
	entities[MDL].v.nextthink = playing ? 101.0f : 0.0f;
}
/* What the retail camera start loop does to one player. */
static void in_camera( int slot )
{
	edict_t *e = clients[slot].edict;
	cof_scene_handle_t h = { &entities[CAM], 3 };
	clients[slot].pViewEntity = &entities[CAM];
	e->v.flags |= FL_FROZEN | FL_NOTARGET; e->v.takedamage = DAMAGE_NO;
	memcpy( private_data[slot] + COF_SCENE_CAMERA, &h, sizeof( h ));
}
/* 1.1.8: the c_start wardrobe: cof_clothesmenu MENU turns on trigger_camera CAM 'clothescamera' (sf 1108) for its user. */
static void wardrobe( int user_slot )
{
	string_t name = 8;
	cof_scene_handle_t h;
	h.edict = clients[user_slot].edict; h.serial = h.edict->serialnumber;
	entities[MENU].v.classname = 7; memcpy( entity_private[MENU] + COF_SCENE_CLOTHES_CAMERA, &name, sizeof( name ));
	entities[CAM].v.targetname = 8; entities[CAM].v.spawnflags = 1108;
	memcpy( entity_private[CAM] + COF_SCENE_CAMERA_USER, &h, sizeof( h ));
}
/* 1.1.8: a running model cutscene at x 300 (MDL) and the camera models it gave the host (12) and the guest (14). */
static void model_scene( edict_t *owner )
{
	cof_scene_handle_t cam;
	model_cutscene( 1 ); entities[MDL].v.spawnflags = 1 | 2 | 64; entities[MDL].v.owner = owner; entities[MDL].v.origin[0] = 300;
	entities[12].v.classname = entities[14].v.classname = 6; entities[12].v.origin[0] = entities[14].v.origin[0] = 300;
	entities[12].v.owner = &entities[1]; entities[14].v.owner = &entities[2];
	entities[1].v.flags |= FL_FROZEN | FL_NOTARGET; entities[2].v.flags |= FL_FROZEN | FL_NOTARGET;
	cam.edict = &entities[12]; cam.serial = 0; memcpy( private_data[0] + COF_SCENE_CAMERA_MODEL, &cam, sizeof( cam ));
	cam.edict = &entities[14]; memcpy( private_data[1] + COF_SCENE_CAMERA_MODEL, &cam, sizeof( cam ));
}
static int logged( const char *s ) { int n = 0; const char *at = log_text; while(( at = strstr( at, s ))) { n++; at++; } return n; }
static qboolean skip( int slot ) { argv0 = "cof_skip"; return SV_CoF_SceneClientCommand( &clients[slot] ); }
/* server frames: how many steps each one ran, summed */
static int steps( int frames ) { int i, n = 0; for( i = 0; i < frames; i++ ) { n += SV_CoF_SceneSkipSteps(); sv.time += 0.05; } return n; }

int main( void )
{
	edict_t *host = &entities[1], *guest = &entities[2];
	cof_scene_handle_t h;
	int index;

	/* A running scene as retail set it up: nothing to repair. */
	setup(); in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame();
	assert( !log_length && clients[1].pViewEntity == &entities[CAM] && SV_CoF_SceneActive());
	assert( SV_CoF_SceneWatching( 100, 101 ) && SV_CoF_SceneWatching( 101, 100 ) && !SV_CoF_SceneWatching( 100, 100 ));

	/* The scene ended while the guest was down; CPR brought them back still in it. */
	setup(); in_camera( 1 ); entities[CAM].v.iuser1 = 0; SV_CoF_SceneFrame();
	assert( !clients[1].pViewEntity && clients[1].netchan.message.last_cmd == svc_setview && clients[1].netchan.message.last_word == 2 );
	assert( !( guest->v.flags & ( FL_FROZEN | FL_NOTARGET )) && guest->v.takedamage == DAMAGE_AIM );
	memcpy( &index, private_data[1] + COF_SCENE_VIEW_INDEX, sizeof( index )); assert( index == 2 );
	memcpy( &h, private_data[1] + COF_SCENE_CAMERA, sizeof( h )); assert( !h.edict && !h.serial );
	assert( logged( "phase=camera-released userid=101 camera=9" ) == 1 && !SV_CoF_SceneActive());
	SV_CoF_SceneFrame(); assert( logged( "camera-released" ) == 1 ); /* once */
	/* the camera entity is gone altogether */
	setup(); in_camera( 1 ); entities[CAM].free = 1; SV_CoF_SceneFrame();
	assert( !clients[1].pViewEntity && !( guest->v.flags & FL_FROZEN ) && logged( "camera-released" ) == 1 );
	/* still down: the game's own Spawn will reset them */
	setup(); in_camera( 1 ); entities[CAM].v.iuser1 = 0; guest->v.health = 0; SV_CoF_SceneFrame();
	assert( clients[1].pViewEntity == &entities[CAM] && !log_length );

	/* Respawned mid-scene: InitHUD gave the view back, never the freeze. */
	setup(); in_camera( 0 ); clients[1].pViewEntity = &entities[CAM]; SV_CoF_SceneFrame();
	assert(( guest->v.flags & FL_FROZEN ) && ( guest->v.flags & FL_NOTARGET ) && guest->v.takedamage == DAMAGE_NO && guest->v.velocity[0] == 0 );
	assert( logged( "phase=camera-held userid=101 camera=9" ) == 1 );
	SV_CoF_SceneFrame(); assert( logged( "camera-held" ) == 1 ); /* once */
	/* a camera that does not take control holds nobody */
	setup(); entities[CAM].v.spawnflags = 112; clients[1].pViewEntity = &entities[CAM]; SV_CoF_SceneFrame();
	assert( !( guest->v.flags & FL_FROZEN ) && !log_length );
	/* another kind of view (a puzzle view, say) is the game's business */
	setup(); entities[CAM].v.classname = 4; clients[1].pViewEntity = &entities[CAM]; entities[CAM].v.iuser1 = 0;
	SV_CoF_SceneFrame(); assert( clients[1].pViewEntity == &entities[CAM] && !log_length );

	/* The host switched the guest's telescope off: the game's own cancel takes the guest out. */
	setup(); h.edict = &entities[TELESCOPE]; h.serial = 5;
	memcpy( private_data[1] + COF_SCENE_TELESCOPE, &h, sizeof( h )); guest->v.flags |= FL_FROZEN;
	SV_CoF_SceneFrame();
	assert( commands == 1 && sender_during && !strcmp( last_command, "telescopecancel" ) && !sender );
	assert( !( guest->v.flags & FL_FROZEN ) && entities[TELESCOPE].v.health == 0 && logged( "telescope-released userid=101" ) == 1 );
	SV_CoF_SceneFrame(); assert( commands == 1 );
	/* a telescope still on, or a stale handle, is left alone */
	setup(); h.edict = &entities[TELESCOPE]; h.serial = 5; entities[TELESCOPE].v.health = 1;
	memcpy( private_data[1] + COF_SCENE_TELESCOPE, &h, sizeof( h )); SV_CoF_SceneFrame(); assert( !commands );
	setup(); h.serial = 4; memcpy( private_data[1] + COF_SCENE_TELESCOPE, &h, sizeof( h )); SV_CoF_SceneFrame(); assert( !commands );

	/* A late joiner held in black by a model cutscene sees again when it ends. */
	setup(); model_cutscene( 1 ); private_data[1][COF_SCENE_CUTSCENE_WAIT] = 1; SV_CoF_SceneFrame(); assert( !fades );
	private_data[1][COF_SCENE_CUTSCENE_WAIT] = 0; SV_CoF_SceneFrame(); assert( fades == 1 && logged( "cutscene-wait-over userid=101" ) == 1 );
	SV_CoF_SceneFrame(); assert( fades == 1 );
	/* a new player in that slot does not inherit the old one's wait */
	setup(); model_cutscene( 1 ); private_data[1][COF_SCENE_CUTSCENE_WAIT] = 1; SV_CoF_SceneFrame();
	clients[1].userid = 102; private_data[1][COF_SCENE_CUTSCENE_WAIT] = 0; SV_CoF_SceneFrame(); assert( !fades );
	/* Review I-2: respawned while the partner climbs a ladder (its camera model
	 * starts the same wait) and no model cutscene runs: released at once. */
	{
		int hud = 9 | 16;
		setup(); model_cutscene( 0 ); private_data[1][COF_SCENE_CUTSCENE_WAIT] = 1;
		memcpy( private_data[1] + COF_SCENE_HIDEHUD, &hud, sizeof( hud )); guest->v.flags |= FL_FROZEN | FL_NOTARGET;
		SV_CoF_SceneFrame();
		memcpy( &hud, private_data[1] + COF_SCENE_HIDEHUD, sizeof( hud ));
		assert( !private_data[1][COF_SCENE_CUTSCENE_WAIT] && !( guest->v.flags & ( FL_FROZEN | FL_NOTARGET )) && hud == 16 && fades == 1 );
		assert( logged( "cutscene-wait-released userid=101" ) == 1 );
		SV_CoF_SceneFrame(); assert( fades == 1 && !logged( "cutscene-wait-over" ));
	}

	/* A map change mid-scene: nobody carries the camera's freeze along. */
	setup(); in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneSettle( "transition" );
	assert( !clients[0].pViewEntity && !clients[1].pViewEntity && !( host->v.flags & FL_FROZEN ) && guest->v.takedamage == DAMAGE_AIM );
	assert( logged( "reason=transition" ) == 2 );
	/* not even a dead partner (a CPR revive there would bring it back); their corpse's damage is left alone */
	setup(); in_camera( 1 ); guest->v.health = 0; SV_CoF_SceneSettle( "transition" );
	assert( !clients[1].pViewEntity && !( guest->v.flags & ( FL_FROZEN | FL_NOTARGET )) && guest->v.takedamage == DAMAGE_NO );

	/* Off, without verified private data, or for a fake client: nothing. */
	setup(); gate = 0; in_camera( 1 ); entities[CAM].v.iuser1 = 0; SV_CoF_SceneFrame(); SV_CoF_SceneSettle( "x" );
	assert( clients[1].pViewEntity && !SV_CoF_SceneActive() && !SV_CoF_SceneWatching( 100, 101 ));
	setup(); in_camera( 1 ); entities[CAM].v.iuser1 = 0; guest->pvPrivateData = NULL; SV_CoF_SceneFrame();
	assert( clients[1].pViewEntity && !log_length );
	setup(); in_camera( 1 ); entities[CAM].v.iuser1 = 0; clients[1].flags = FCL_FAKECLIENT; SV_CoF_SceneFrame();
	assert( clients[1].pViewEntity && !log_length );

	/* Watching together needs the same running camera. */
	setup(); in_camera( 0 ); assert( !SV_CoF_SceneWatching( 100, 101 ));
	in_camera( 1 ); entities[CAM].v.iuser1 = 0; assert( !SV_CoF_SceneWatching( 100, 101 ));

	/* cof_skip (Enter): with everyone frozen in a running camera, the server runs ten
	 * steps a frame for the party; said to both, logged once. */
	setup(); SV_CoF_SceneInit(); in_camera( 0 ); in_camera( 1 );
	assert( steps( 3 ) == 3 ); /* nobody asked */
	assert( skip( 1 ) && logged( "phase=cutscene-skip userid=101" ) == 1 );
	assert( !strcmp( told[0], "Little Rascal 2 skips the cutscene (Enter: watch it)." ) && !strcmp( told[0], told[1] ));
	assert( steps( 4 ) == 40 );
	/* pressed again: watching again, at the normal speed */
	assert( skip( 0 ) && logged( "cutscene-skip-stopped userid=100" ) == 1 && steps( 2 ) == 2 );
	assert( !strcmp( told[1], "Little Rascal 1 is watching the cutscene again." ));
	/* the camera ends: two seconds later the skip is over, and a later scene plays normally */
	assert( skip( 0 ) && steps( 1 ) == 10 );
	entities[CAM].v.iuser1 = 0; SV_CoF_SceneFrame(); /* both released */
	assert( steps( 50 ) == 50 && logged( "cutscene-skip-over" ) == 1 );
	entities[CAM].v.iuser1 = 1; in_camera( 0 ); in_camera( 1 ); assert( steps( 3 ) == 3 );
	/* somebody still playing (a camera that leaves them free, or no camera at all): nothing to skip */
	setup(); in_camera( 0 ); told_count = 0;
	assert( skip( 0 ) && !strcmp( told[0], "No cutscene to skip right now." ) && told_count == 1 && steps( 3 ) == 3 );
	/* ...and a partner who starts to play mid-skip slows it at once */
	setup(); in_camera( 0 ); in_camera( 1 ); assert( skip( 0 ) && steps( 1 ) == 10 );
	guest->v.flags &= ~FL_FROZEN; clients[1].pViewEntity = NULL; assert( steps( 1 ) == 1 );
	/* the dead do not hold it up; nobody alive: nothing to skip */
	setup(); in_camera( 0 ); guest->v.health = 0; assert( skip( 0 ) && steps( 1 ) == 10 );
	setup(); host->v.health = guest->v.health = 0; assert( skip( 0 ) && !strcmp( told[0], "No cutscene to skip right now." ));
	/* a model cutscene holds the party too, through each one's own camera model */
	{
		cof_scene_handle_t cam = { &entities[12], 2 };
		entities[12].v.classname = 4; entities[12].serialnumber = 2;
		setup(); entities[12].serialnumber = 2; model_cutscene( 1 ); host->v.flags |= FL_FROZEN; guest->v.flags |= FL_FROZEN;
		assert( skip( 1 ) && !strcmp( told[1], "No cutscene to skip right now." )); /* frozen, but not by it (a ladder, a puzzle) */
		memcpy( private_data[0] + 0x388, &cam, sizeof( cam )); memcpy( private_data[1] + 0x388, &cam, sizeof( cam ));
		assert( skip( 1 ) && steps( 1 ) == 10 );
		guest->v.fuser3 = 10; assert( steps( 1 ) == 1 ); /* grabbed: their presses count against the clock */
		guest->v.fuser3 = 0; assert( steps( 1 ) == 10 );
		model_cutscene( 0 ); assert( steps( 1 ) == 1 );
	}
	/* the stepped loop runs only with sv_fps 0: otherwise refused, not announced */
	setup(); in_camera( 0 ); in_camera( 1 ); sv_fps_value = 30;
	assert( skip( 0 ) && !strcmp( told[0], "Cutscene skipping needs sv_fps 0." ) && told_count == 1 && steps( 1 ) == 1 );
	/* a new map ends a skip */
	setup(); in_camera( 0 ); in_camera( 1 ); assert( skip( 0 ) && steps( 1 ) == 10 );
	svs.spawncount++; assert( steps( 1 ) == 1 );
	/* turned off (cof_skip_speed below 2), story off, a fake client, another command */
	setup(); in_camera( 0 ); in_camera( 1 ); skip_speed.value = 1;
	assert( skip( 0 ) && !strcmp( told[0], "Cutscene skipping is off (cof_skip_speed)." ) && steps( 1 ) == 1 );
	setup(); gate = 0; in_camera( 0 ); in_camera( 1 ); assert( !skip( 0 ) && steps( 1 ) == 1 );
	setup(); in_camera( 0 ); in_camera( 1 ); clients[1].flags = FCL_FAKECLIENT; assert( skip( 1 ) && !told_count && steps( 1 ) == 1 );
	setup(); argv0 = "cof_warp"; assert( !SV_CoF_SceneClientCommand( &clients[0] ));

	/* 1.1.8 (K1): the wardrobe's camera views and freezes everyone, but its menu is its user's alone: the partner is
	 * let go at once (health HUD back), never held again, and not "watching" with the user. */
	{
		int hud = 8 | 1;
		setup(); wardrobe( 0 ); in_camera( 0 ); in_camera( 1 );
		memcpy( private_data[1] + COF_SCENE_HIDEHUD, &hud, sizeof( hud ));
		SV_CoF_SceneFrame();
		assert( clients[0].pViewEntity == &entities[CAM] && ( host->v.flags & FL_FROZEN ));
		assert( !clients[1].pViewEntity && !( guest->v.flags & ( FL_FROZEN | FL_NOTARGET )) && guest->v.takedamage == DAMAGE_AIM );
		memcpy( &hud, private_data[1] + COF_SCENE_HIDEHUD, sizeof( hud )); assert( hud == 1 );
		assert( logged( "phase=wardrobe-camera camera=9 menu=13" ) == 1 && logged( "camera-released userid=101 camera=9 reason=wardrobe" ) == 1 );
		assert( !SV_CoF_SceneWatching( 100, 101 ) && !commands );
		/* a respawn re-attached to it is let go again, not held */
		clients[1].pViewEntity = &entities[CAM]; SV_CoF_SceneFrame();
		assert( !clients[1].pViewEntity && !( guest->v.flags & FL_FROZEN ) && !logged( "camera-held" ) && logged( "reason=wardrobe" ) == 2 );
		/* the user left with the menu open: closed by the game's own closeclothes, sent as the partner */
		clients[0].state = cs_zombie; SV_CoF_SceneFrame();
		assert( commands == 1 && !strcmp( last_command, "closeclothes" ) && command_sender == guest && !sender );
		assert( logged( "wardrobe-closed camera=9 sender=101 reason=user-gone" ) == 1 && !entities[CAM].v.iuser1 );
		SV_CoF_SceneFrame(); assert( commands == 1 );
	}
	/* the user died in it: closed as the user; a camera the game left running is closed again only 5 s later */
	setup(); wardrobe( 1 ); in_camera( 0 ); in_camera( 1 ); guest->v.health = 0; wardrobe_closes = 0;
	SV_CoF_SceneFrame(); assert( commands == 1 && command_sender == guest && logged( "sender=101 reason=user-dead" ) == 1 );
	sv.time += 1.0; SV_CoF_SceneFrame(); assert( commands == 1 );
	sv.time += 5.0; SV_CoF_SceneFrame(); assert( commands == 2 );
	/* the user still there: nothing closed, the partner let go; and the user's partner was never watching with them */
	setup(); wardrobe( 1 ); in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame();
	assert( !commands && clients[1].pViewEntity == &entities[CAM] && !clients[0].pViewEntity );
	entities[CAM].v.iuser1 = 0; clients[1].pViewEntity = NULL; guest->v.flags = 0; assert( !SV_CoF_SceneWatching( 100, 101 ));
	/* a camera no wardrobe names stays everyone's */
	setup(); entities[MENU].v.classname = 7; entities[CAM].v.targetname = 8; in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame();
	assert( clients[0].pViewEntity && clients[1].pViewEntity && !log_length );

	/* 1.1.8 (K4): a merged map. A camera of section 0 holds nobody in section 1 (another original level). */
	{
		int hud = 8 | 16 | 1, bars = 1;
		setup(); merged = 1; guest->v.origin[2] = 2000; entities[CAM].v.spawnflags = 116 | 32; entities[CAM].v.iuser2 = 1;
		in_camera( 0 ); in_camera( 1 );
		memcpy( private_data[1] + COF_SCENE_HIDEHUD, &hud, sizeof( hud )); memcpy( private_data[1] + COF_SCENE_BARS, &bars, sizeof( bars ));
		SV_CoF_SceneFrame();
		assert( clients[0].pViewEntity == &entities[CAM] && !clients[1].pViewEntity && !( guest->v.flags & FL_FROZEN ));
		memcpy( &hud, private_data[1] + COF_SCENE_HIDEHUD, sizeof( hud )); memcpy( &bars, private_data[1] + COF_SCENE_BARS, sizeof( bars ));
		assert( hud == 1 && bars == 0 && logged( "camera-released userid=101 camera=9 reason=section" ) == 1 );
		assert( !SV_CoF_SceneWatching( 100, 101 ));
		/* moved to section 1 while it ran: let go, and not taken along by its end */
		setup(); merged = 1; in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame(); guest->v.origin[2] = 2000; SV_CoF_SceneFrame();
		assert( !clients[1].pViewEntity && logged( "reason=section" ) == 1 );
		entities[CAM].v.iuser1 = 0; clients[0].pViewEntity = NULL; host->v.flags = 0; assert( !SV_CoF_SceneWatching( 100, 101 ));
		/* the same section: everyone watches */
		setup(); merged = 1; guest->v.origin[2] = 500; in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame();
		assert( clients[1].pViewEntity && !log_length && SV_CoF_SceneWatching( 100, 101 ));
	}
	/* ...nor does a model cutscene of section 0: the guest's camera model goes, control and HUD come back */
	{
		uint32_t raw = 0x1234; int hud = 8 | 1 | 16;
		setup(); merged = 1; model_scene( host ); guest->v.origin[2] = 2000;
		memcpy( private_data[1] + COF_SCENE_DEATH_SCENE, &raw, 4 ); memcpy( private_data[1] + COF_SCENE_HIDEHUD, &hud, 4 );
		SV_CoF_SceneFrame();
		memcpy( &raw, private_data[1] + COF_SCENE_DEATH_SCENE, 4 ); memcpy( &hud, private_data[1] + COF_SCENE_HIDEHUD, 4 );
		memcpy( &h, private_data[1] + COF_SCENE_CAMERA_MODEL, sizeof( h ));
		assert( !raw && !h.edict && ( entities[14].v.flags & FL_KILLME ) && !( guest->v.flags & ( FL_FROZEN | FL_NOTARGET )) && hud == 0 );
		memcpy( &h, private_data[0] + COF_SCENE_CAMERA_MODEL, sizeof( h ));
		assert( h.edict == &entities[12] && !( entities[12].v.flags & FL_KILLME ) && ( host->v.flags & FL_FROZEN ));
		assert( logged( "cutscene-released userid=101 cutscene=11 reason=section" ) == 1 );
		SV_CoF_SceneFrame(); assert( logged( "cutscene-released" ) == 1 );
		/* never its owner, whom the cutscene needs, wherever they are */
		setup(); merged = 1; model_scene( guest ); guest->v.origin[2] = 2000; SV_CoF_SceneFrame();
		assert(( guest->v.flags & FL_FROZEN ) && !( entities[14].v.flags & FL_KILLME ) && !log_length );
		/* the same section, or not merged: left alone */
		setup(); merged = 1; model_scene( host ); SV_CoF_SceneFrame(); assert(( guest->v.flags & FL_FROZEN ) && !log_length );
		setup(); model_scene( host ); guest->v.origin[2] = 2000; SV_CoF_SceneFrame(); assert(( guest->v.flags & FL_FROZEN ) && !log_length );
	}
	/* a late joiner of section 1 is not kept in the black wait for a model cutscene of section 0 */
	setup(); merged = 1; model_cutscene( 1 ); guest->v.origin[2] = 2000; private_data[1][COF_SCENE_CUTSCENE_WAIT] = 1; SV_CoF_SceneFrame();
	assert( logged( "cutscene-wait-released userid=101" ) == 1 );
	setup(); merged = 1; model_cutscene( 1 ); private_data[1][COF_SCENE_CUTSCENE_WAIT] = 1; SV_CoF_SceneFrame(); assert( !log_length );

	/* 1.1.8 (M3 finding 2a): frozen in one running model cutscene is watching together (each through their own
	 * camera model); not a partner left free, not a ladder's model, not once it is over. */
	setup(); model_scene( host ); assert( SV_CoF_SceneWatching( 100, 101 ) && SV_CoF_SceneWatching( 101, 100 ));
	guest->v.flags &= ~FL_FROZEN; assert( !SV_CoF_SceneWatching( 100, 101 ));
	guest->v.flags |= FL_FROZEN; entities[14].v.origin[0] = 900; assert( !SV_CoF_SceneWatching( 100, 101 ));
	entities[14].v.origin[0] = 300; model_cutscene( 0 ); assert( !SV_CoF_SceneWatching( 100, 101 ));

	/* 1.1.8 (M1 finding 5): both watched one camera to its end: still together for 2 s, for the scene's closing
	 * teleport that fires as the camera ends. */
	setup(); in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame();
	entities[CAM].v.iuser1 = 0; clients[0].pViewEntity = clients[1].pViewEntity = NULL; host->v.flags = guest->v.flags = 0;
	sv.time += 0.05; SV_CoF_SceneFrame(); assert( SV_CoF_SceneWatching( 100, 101 ) && SV_CoF_SceneWatching( 101, 100 ));
	sv.time += 1.9; assert( SV_CoF_SceneWatching( 100, 101 ));
	sv.time += 0.2; assert( !SV_CoF_SceneWatching( 100, 101 ));
	/* not after a level change (its time starts again): neither before nor after the first frame there */
	setup(); in_camera( 0 ); in_camera( 1 ); sv.time = 300.0; SV_CoF_SceneFrame();
	entities[CAM].v.iuser1 = 0; clients[0].pViewEntity = clients[1].pViewEntity = NULL; host->v.flags = guest->v.flags = 0;
	svs.spawncount++; sv.time = 301.0; assert( !SV_CoF_SceneWatching( 100, 101 ));
	SV_CoF_SceneFrame(); assert( !SV_CoF_SceneWatching( 100, 101 ));
	/* not while it still runs (one left it); not someone new in that slot; not two cameras */
	setup(); in_camera( 0 ); in_camera( 1 ); SV_CoF_SceneFrame(); clients[1].pViewEntity = NULL; guest->v.flags = 0;
	assert( !SV_CoF_SceneWatching( 100, 101 ));
	entities[CAM].v.iuser1 = 0; clients[0].pViewEntity = NULL; host->v.flags = 0; assert( SV_CoF_SceneWatching( 100, 101 ));
	clients[1].userid = 102; assert( !SV_CoF_SceneWatching( 100, 102 ));
	setup(); in_camera( 0 ); SV_CoF_SceneFrame(); entities[12].v.classname = 2; entities[12].v.iuser1 = 1; clients[1].pViewEntity = &entities[12];
	guest->v.flags |= FL_FROZEN | FL_NOTARGET; guest->v.takedamage = DAMAGE_NO; SV_CoF_SceneFrame();
	entities[CAM].v.iuser1 = entities[12].v.iuser1 = 0; clients[0].pViewEntity = clients[1].pViewEntity = NULL;
	assert( !SV_CoF_SceneWatching( 100, 101 ));

	puts( "scene glue scenarios passed" );
	return 0;
}
