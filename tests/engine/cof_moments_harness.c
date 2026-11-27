/* Story moments (sv_cof_moments.c) against engine doubles: no game DLL,
 * networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
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
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define SOLID_NOT 0
#define SOLID_TRIGGER 1
#define SOLID_SLIDEBOX 3
#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define FL_GODMODE (1<<6)
#define FL_NOTARGET (1<<7)
#define FL_FROZEN (1<<12)
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define IN_FORWARD (1U<<3)
#define IN_BACK (1U<<4)
#define IN_USE (1<<5)
#define COF_RETAIL_SLOTS 12
#define COF_RETAIL_ITEM 64
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
#define Q_atoi atoi
#define Q_snprintf snprintf
#define Q_strlen strlen
#define Q_strncmp strncmp
#define SetBits(f,b) ((f)|=(b))
#define ClearBits(f,b) ((f)&=~(b))
#define FBitSet(f,b) ((f)&(b))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
#define VectorAdd(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorClear(x) ((x)[0]=(x)[1]=(x)[2]=0)
#define VectorScale(in,s,out) ((out)[0]=(in)[0]*(s),(out)[1]=(in)[1]*(s),(out)[2]=(in)[2]*(s))
#define MSG_ONE 1
#define MSG_ALL 2
#define MSG_ONE_UNRELIABLE 8
#define VectorIsNull(v) ((v)[0] == 0.0f && (v)[1] == 0.0f && (v)[2] == 0.0f)
typedef struct edict_s edict_t;
typedef struct {
	string_t classname, target, targetname, message, model;
	vec3_t origin, angles, velocity, avelocity, absmin, absmax;
	float health, nextthink, fuser3, dmg;
	int deadflag, flags, spawnflags, iuser1, solid, modelindex, button;
	edict_t *owner;
} entvars_t;
/* brush models: hull 1 is the brush grown by a player's half size, so a point in it is a player touching the brush */
typedef struct { int firstclipnode; vec3_t clip_mins; } hull_t;
enum { mod_bad, mod_brush };
typedef struct { int type; hull_t hulls[4]; vec3_t shape_min, shape_max; } model_t;
static model_t models[4];
static model_t *SV_ModelHandle( int index ) { return index > 0 && index < 4 ? &models[index] : NULL; }
static int PM_HullPointContents( hull_t *hull, int num, const vec3_t p )
{
	static const float half[3] = { 16, 16, 36 };
	const model_t *mod = &models[num];
	int k;
	assert( hull == &models[num].hulls[1] && hull->clip_mins[2] == -36.0f );
	for( k = 0; k < 3; k++ ) if( p[k] < mod->shape_min[k] - half[k] || p[k] > mod->shape_max[k] + half[k] ) return CONTENTS_EMPTY;
	return CONTENTS_SOLID;
}
struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; };
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict, *pViewEntity; } sv_client_t;
typedef struct { int buttons; } usercmd_t;
static struct { int state; double time; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { edict_t *edicts; int numEntities; struct { void (*pfnClientKill)( edict_t * ); } dllFuncs; } svgame;
static edict_t entities[24];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[24][0x2900];
static const char *strings[] = { "", "player", "trigger_once", "cof_killplayer", "die", "cof_phonecall", "sophiecall",
	"gateinter", "cof_ladder_manager", "cof_ladder_manager_use", "lad1", "wheelchair", "camera_model", "cof_yesno",
	"ryesno", "yesno", "multi_manager", "lad2",
	/* 18 */ "cof_teleport", "info_teleport_destination", "trigger_hurt", "multifallheh", "teleportfall", "teleportfall2",
	/* 24 */ "kallamig", "multiroofscene", "rsstart", "globalr", "boat", "monster", "trigger_relay", "ladddd",
	/* 32 */ "DelayedUse", "gfx/vgui/boss/simon_health_empty.tga", "gfx/vgui/boss/simon_health_full.tga", "sicksimon",
	/* 36 */ "*7", "env_fade", "models/ladder_climb.mdl", "models/strangle_simon.mdl" };
static char log_text[16384];
static size_t log_length;
static int gate, kills[MAX_CLIENTS + 1], has_phone[MAX_CLIENTS], fired;
static char fired_name[64], prompts[MAX_CLIENTS][128], argv0[32], argv1[32];
static edict_t *preferred;
static int argc;
#define BASE 0x10000000u

static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + 24 && !e->free; }
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return SV_IsValidEdict( e ) && need <= 0x2900 ? e->pvPrivateData : NULL; }
static uintptr_t SV_CoF_StoryBase( void ) { return gate ? BASE : 0; }
static qboolean SV_CoF_StoryFireTargets( const char *name, void *activator, void *caller ) { fired++; strcpy( fired_name, name ); return true; }
static void SV_CoF_PreferFirst( edict_t *e ) { preferred = e; }
static qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	memset( slots, 0, COF_RETAIL_SLOTS * COF_RETAIL_ITEM );
	if( has_phone[cl - clients] ) strcpy( slots[has_phone[cl - clients] == 2 ? 9 : 2], "inventoryitems/weapons/weapon_mobile.txt" );
	return true;
}
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	snprintf( prompts[cl - clients], sizeof( prompts[0] ), "%d:%s", colour, text ); return true;
}
static int logos_to[MAX_CLIENTS], logo_value, fades_to[MAX_CLIENTS], boats_to[MAX_CLIENTS], faded_in[MAX_CLIENTS], links, controls_given[24];
static int bossbars_to[MAX_CLIENTS], bossbar_size, credits_to[MAX_CLIENTS], credits_size;
static byte credits[256];
static byte fade_value[2], bossbar[192];
static int views_given_back[MAX_CLIENTS];
static void SV_CoF_MomentsMessage( edict_t *to, const char *name, const byte *payload, int size );
/* as the engine: the message goes out, and is seen by the moments hook like any other */
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	assert( reliable );
	if( !strcmp( name, "CoFLogo" )) { assert( size == 1 ); logos_to[NUM_FOR_EDICT( to ) - 1]++; logo_value = payload[0]; }
	else if( !strcmp( name, "GreyFade" )) { assert( size == 2 ); fades_to[NUM_FOR_EDICT( to ) - 1]++; memcpy( fade_value, payload, 2 ); }
	else if( !strcmp( name, "Credits" )) { credits_to[NUM_FOR_EDICT( to ) - 1]++; memcpy( credits, payload, size ); credits_size = size; }
	else if( !strcmp( name, "BossBar" )) { bossbars_to[NUM_FOR_EDICT( to ) - 1]++; memcpy( bossbar, payload, size ); bossbar_size = size; }
	else if( !strcmp( name, "AttachCam" )) { assert( size == 3 && !payload[0] && !payload[1] && !payload[2] ); views_given_back[NUM_FOR_EDICT( to ) - 1]++; }
	else { assert( !strcmp( name, "Boat" ) && size == 2 && payload[0] == 0xFF && payload[1] == 0xFF ); boats_to[NUM_FOR_EDICT( to ) - 1]++; }
	SV_CoF_MomentsMessage( to, name, payload, size );
	return true;
}
static qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a )
{
	assert( duration == 1.0f && hold == 0.0f && !flags && !r && !g && !b && a == 255 );
	faded_in[cl - clients]++; return true;
}
static void SV_LinkEdict( edict_t *e, qboolean touch ) { assert( !touch ); links++; }
/* merged maps (sv_cof_sections.c): sections stacked by height, section k from z = 1000 k; not merged: -1 */
static int merged;
static int SV_CoF_PointSection( const vec3_t origin ) { return merged ? ( origin[2] < 1000.0f ? 0 : 1 ) : -1; }
static int SV_CoF_PlayerSection( const edict_t *e ) { return e ? SV_CoF_PointSection( e->v.origin ) : -1; }
/* the entity whose think runs (sv_cof_rescue.c) */
static edict_t *running_think;
static edict_t *SV_CoF_RescueThinker( void ) { return running_think; }
#include "cof_strip.h"
/* the party's move beside a partner (sv_cof_party.c): set down 35 u from the anchor */
static int section_moves;
static qboolean SV_CoF_PartySectionMove( sv_client_t *cl, const vec3_t landing, sv_client_t *anchor, const float *fallback )
{
	assert( anchor && landing == anchor->edict->v.origin && !fallback );
	section_moves++;
	VectorCopy( anchor->edict->v.origin, cl->edict->v.origin ); cl->edict->v.origin[0] += 35.0f;
	return true;
}
/* CBasePlayer::EnableControl( TRUE ), thiscall 100DEFF0 */
static qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
	int i;
	assert( rva == 0xDEFF0 && arg == (void *)1 );
	for( i = 0; i < 24; i++ ) if( private_data[i] == self ) controls_given[i]++;
	return true;
}
static const char *Cmd_Argv( int i ) { return i == 0 ? argv0 : i == 1 ? argv1 : ""; }
static int Cmd_Argc( void ) { return argc; }
static void Con_Printf( const char *fmt, ... )
{
	va_list args; int n;
	va_start( args, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, args );
	va_end( args );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static void client_kill( edict_t *e ) { kills[NUM_FOR_EDICT( e )]++; e->v.health = 0; }

#include "moments_under_test.c"

static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities )); memset( clients, 0, sizeof( clients ));
	memset( private_data, 0, sizeof( private_data )); memset( prompts, 0, sizeof( prompts ));
	memset( kills, 0, sizeof( kills )); memset( has_phone, 0, sizeof( has_phone ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	gate = 1; fired = 0; fired_name[0] = 0; preferred = NULL;
	sv.state = ss_active; sv.time = 100; svs.maxclients = 2; svs.clients = clients; svs.spawncount++;
	svgame.edicts = entities; svgame.numEntities = 24; svgame.dllFuncs.pfnClientKill = client_kill;
	for( i = 0; i < 24; i++ ) { entities[i].pvPrivateData = private_data[i]; entities[i].serialnumber = 1; }
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned; clients[i].userid = 100 + i; clients[i].edict = &entities[i + 1];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		entities[i + 1].v.classname = 1; entities[i + 1].v.health = 100; entities[i + 1].v.origin[0] = (float)i * 500;
	}
	for( i = 3; i < 24; i++ ) entities[i].free = 1;
	memset( logos_to, 0, sizeof( logos_to )); logo_value = -1;
	memset( fades_to, 0, sizeof( fades_to )); memset( boats_to, 0, sizeof( boats_to )); memset( faded_in, 0, sizeof( faded_in ));
	memset( controls_given, 0, sizeof( controls_given )); links = 0; memset( models, 0, sizeof( models )); section_moves = 0;
	memset( bossbars_to, 0, sizeof( bossbars_to )); bossbar_size = 0; memset( credits_to, 0, sizeof( credits_to )); credits_size = 0;
	merged = 0; running_think = NULL;
	memset( views_given_back, 0, sizeof( views_given_back ));
	SV_CoF_MomentsReset();
}
static edict_t *spawn( int i, string_t classname, string_t targetname, string_t target )
{
	edict_t *e = &entities[i];
	e->free = 0; e->v.classname = classname; e->v.targetname = targetname; e->v.target = target;
	return e;
}
static void set_handle( int entity, int offset, edict_t *to )
{
	cof_moment_handle_t h; h.edict = to; h.serial = to ? to->serialnumber : 0;
	memcpy( private_data[entity] + offset, &h, sizeof( h ));
}
static edict_t *get_handle( int entity, int offset )
{
	cof_moment_handle_t h; memcpy( &h, private_data[entity] + offset, sizeof( h )); return h.edict;
}
static void set_think( int entity, uint32_t rva ) { uint32_t v = rva ? BASE + rva : 0; memcpy( private_data[entity] + COF_THINK, &v, 4 ); }
static uint32_t get_think( int entity ) { uint32_t v; memcpy( &v, private_data[entity] + COF_THINK, 4 ); return v; }
static int logged( const char *s ) { int n = 0; const char *at = log_text; while(( at = strstr( at, s ))) { n++; at++; } return n; }
static qboolean command( int slot, const char *a0, const char *a1 ) { strcpy( argv0, a0 ); strcpy( argv1, a1 ); argc = 2; return SV_CoF_MomentsClientCommand( &clients[slot] ); }
/* 1.1.8: a multi_manager (CMultiManager private data) firing these names */
static void mm_spawn( int i, string_t name, int count, const string_t *targets )
{
	int k;
	spawn( i, 16, name, 0 );
	memcpy( private_data[i] + COF_MM_COUNT, &count, 4 );
	for( k = 0; k < count && k < 16; k++ ) memcpy( private_data[i] + COF_MM_NAMES + k * 4, &targets[k], 4 );
}
static void set_vec( vec3_t v, float x, float y, float z ) { v[0] = x; v[1] = y; v[2] = z; }
/* 1.1.8: the c_roofs fall: trigger_once 10 -> multi_manager 11 'multifallheh' -> cof_teleport 12 'teleportfall' ->
 * info_teleport_destination 13 'teleportfall2' (1911 532 381), in trigger_hurt 14 (brush model 1, dmg 1000, on). */
static void fall_trap( void )
{
	string_t fall[] = { 22 };
	edict_t *hurt;
	spawn( 10, 2, 0, 21 ); mm_spawn( 11, 21, 1, fall );
	spawn( 12, 18, 22, 23 ); set_vec( spawn( 13, 19, 23, 0 )->v.origin, 1911, 532, 381 );
	hurt = spawn( 14, 20, 0, 0 ); hurt->v.dmg = 1000; hurt->v.solid = SOLID_TRIGGER; hurt->v.modelindex = 1;
	models[1].type = mod_brush; models[1].hulls[1].firstclipnode = 1; set_vec( models[1].hulls[1].clip_mins, -16, -16, -36 );
	set_vec( models[1].shape_min, 1800, 400, 300 ); set_vec( models[1].shape_max, 2000, 600, 450 );
	set_vec( hurt->v.absmin, 1800, 400, 300 ); set_vec( hurt->v.absmax, 2000, 600, 450 );
}
/* a ladder (manager 15, models/ladder_climb.mdl) and a camera model (18) of the given model attached to the guest, as a
 * climb leaves them (P+0x388, P+0x5AC) */
static edict_t *attach_camera( string_t model )
{
	edict_t *manager = spawn( 15, 8, 10, 0 ), *camera = spawn( 18, 12, 0, 0 );
	uint32_t scene = 0x5678;
	manager->v.model = 38; manager->v.owner = &entities[2];
	memcpy( private_data[15] + COF_LADDER_CLIMBER, &scene, 4 );
	camera->v.model = model; camera->v.owner = &entities[2];
	set_handle( 2, COF_PL_CAMERA_HANDLE, camera ); memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &scene, 4 );
	entities[2].v.flags = FL_NOTARGET;
	return camera;
}
static uint32_t word( int entity, int offset ) { uint32_t v; memcpy( &v, private_data[entity] + offset, 4 ); return v; }
static void frames( double from, double to ) { for( sv.time = from; sv.time < to + 0.001; sv.time += 0.5 ) SV_CoF_MomentsFrame(); }
static void yesno_message( int slot, int index )
{
	byte payload[32]; int n = (int)strlen( "gfx/yesno.tga" ) + 1;
	memcpy( payload, "gfx/yesno.tga", n ); payload[n] = (byte)( index & 0xFF ); payload[n + 1] = (byte)( index >> 8 );
	SV_CoF_MomentsMessage( clients[slot].edict, "YesNo", payload, n + 2 );
}

int main( void )
{
	edict_t *host = &entities[1], *guest = &entities[2], *call, *chair, *manager, *use, *ladder_camera;
	usercmd_t cmd;
	int bits, value;
	float suicide;

	/* Fall-death floor: one fall kills only the faller; the floor stays for the partner. */
	setup(); spawn( 10, 2, 0, 4 ); spawn( 11, 3, 4, 0 )->v.spawnflags = 39;
	suicide = 999.0f; memcpy( private_data[2] + COF_PL_SUICIDE, &suicide, 4 );
	assert( SV_CoF_MomentsTriggerTouch( &entities[10], guest ));
	assert( kills[2] == 1 && kills[1] == 0 && logged( "phase=fall-death userid=101 trigger=10" ) == 1 && logged( "fall-floor trigger=10 target=die" ) == 1 );
	memcpy( &bits, private_data[2] + COF_PL_DEATHBITS, 4 ); assert( bits == ( 39 & 0x3F ));
	memcpy( &suicide, private_data[2] + COF_PL_SUICIDE, 4 ); assert( suicide == 0.0f );
	/* the faller's body keeps touching it: nothing more, and still not dispatched */
	assert( SV_CoF_MomentsTriggerTouch( &entities[10], guest ) && kills[2] == 1 );
	/* the partner can still fall there */
	assert( SV_CoF_MomentsTriggerTouch( &entities[10], host ) && kills[1] == 1 );
	/* any other trigger, a non-player, or the story layer off: the game handles it */
	spawn( 12, 2, 0, 6 ); assert( !SV_CoF_MomentsTriggerTouch( &entities[12], host ));
	assert( !SV_CoF_MomentsTriggerTouch( &entities[10], &entities[12] ));
	gate = 0; assert( !SV_CoF_MomentsTriggerTouch( &entities[10], host )); gate = 1;
	/* a floor switched off (iuser1) is the game's to refuse; so is any touch before the server runs */
	setup(); spawn( 10, 2, 0, 4 )->v.iuser1 = 1; spawn( 11, 3, 4, 0 );
	assert( !SV_CoF_MomentsTriggerTouch( &entities[10], guest ) && !kills[2] );
	entities[10].v.iuser1 = 0; sv.state = 1; assert( !SV_CoF_MomentsTriggerTouch( &entities[10], guest )); sv.state = ss_active;
	/* the doctor's execution (c_attic) goes through a multi_manager: retail as is */
	setup(); spawn( 10, 2, 0, 16 ); spawn( 13, 16, 16, 4 ); spawn( 11, 3, 4, 0 );
	assert( !SV_CoF_MomentsTriggerTouch( &entities[10], guest ) && !kills[2] );

	/* A call ringing for someone without a phone rings a partner who has one. */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[0] = 1;
	SV_CoF_MomentsFrame();
	assert( get_handle( 14, COF_CALL_PLAYER ) == host && logged( "phase=call-retargeted call=sophiecall entity=1" ) == 1 );
	assert( !strcmp( prompts[0], "5:Your phone is ringing. Take it out to answer." ) && !strcmp( prompts[1], "5:Little Rascal 1 is taking the call." ));
	/* Review minor 1: the ring left on the guest goes, and the host hears it ring */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[0] = 1; private_data[14][COF_CALL_RANG] = 1;
	value = 3; memcpy( private_data[2] + COF_PL_CALL_UI, &value, 4 ); value = 1; memcpy( private_data[2] + COF_PL_BARS, &value, 4 );
	value = 8 | 1; memcpy( private_data[2] + COF_PL_HIDEHUD, &value, 4 ); guest->v.fuser3 = 5.0f;
	SV_CoF_MomentsFrame();
	assert( !private_data[14][COF_CALL_RANG] && guest->v.fuser3 == 0.0f );
	memcpy( &value, private_data[2] + COF_PL_CALL_UI, 4 ); assert( value == 0 );
	memcpy( &value, private_data[2] + COF_PL_BARS, 4 ); assert( value == 0 );
	memcpy( &value, private_data[2] + COF_PL_HIDEHUD, 4 ); assert( value == 1 );
	/* ringing for someone who can take it: left alone */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[1] = has_phone[0] = 1; SV_CoF_MomentsFrame();
	assert( get_handle( 14, COF_CALL_PLAYER ) == guest && !log_length );
	/* 1.1.9 (AI2): ringing someone who can take it but does not (AFK) for 20 s: it rings the nearest other who can,
	 * and back again 20 s later; an answer starts the count again */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[1] = has_phone[0] = 1; private_data[14][COF_CALL_RANG] = 1;
	SV_CoF_MomentsFrame(); sv.time = 119.9; call->v.nextthink = 120.0f; SV_CoF_MomentsFrame();
	assert( get_handle( 14, COF_CALL_PLAYER ) == guest && !log_length );
	sv.time = 120.0; SV_CoF_MomentsFrame();
	assert( get_handle( 14, COF_CALL_PLAYER ) == host && !private_data[14][COF_CALL_RANG] && logged( "call-retargeted call=sophiecall entity=1" ) == 1 );
	assert( !strcmp( prompts[0], "5:Your phone is ringing. Take it out to answer." ) && !strcmp( prompts[1], "5:Little Rascal 1 is taking the call." ));
	sv.time = 139.9; SV_CoF_MomentsFrame(); assert( get_handle( 14, COF_CALL_PLAYER ) == host );
	sv.time = 140.0; SV_CoF_MomentsFrame(); assert( get_handle( 14, COF_CALL_PLAYER ) == guest && logged( "call-retargeted call=sophiecall entity=2" ) == 1 );
	set_think( 14, COF_CALL_TALK ); sv.time = 150.0; SV_CoF_MomentsFrame();                   /* the guest answered */
	set_think( 14, COF_CALL_WAIT ); sv.time = 165.0; SV_CoF_MomentsFrame();                   /* rings again later */
	sv.time = 184.9; SV_CoF_MomentsFrame(); assert( get_handle( 14, COF_CALL_PLAYER ) == guest );
	sv.time = 185.0; SV_CoF_MomentsFrame(); assert( get_handle( 14, COF_CALL_PLAYER ) == host );
	/* nobody else can take it: it keeps ringing the one it rings */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[1] = 1; SV_CoF_MomentsFrame();
	sv.time = 130.0; call->v.nextthink = 130.1f; SV_CoF_MomentsFrame(); assert( get_handle( 14, COF_CALL_PLAYER ) == guest && !log_length );
	/* the player in a call already is not a candidate */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 100.1f;
	set_handle( 14, COF_CALL_PLAYER, guest ); has_phone[0] = 1; private_data[1][COF_PL_IN_CALL] = 1; SV_CoF_MomentsFrame();
	assert( get_handle( 14, COF_CALL_PLAYER ) == guest );

	/* The game dropped the call (the guest died talking): their lock is cleared and
	 * the call starts again, ringing the host. */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_TALK ); call->v.nextthink = 0;
	set_handle( 14, COF_CALL_PLAYER, guest ); set_handle( 14, COF_CALL_USER, guest );
	private_data[2][COF_PL_IN_CALL] = 1; value = 1; memcpy( private_data[2] + COF_PL_BARS, &value, 4 );
	value = 8 | 1; memcpy( private_data[2] + COF_PL_HIDEHUD, &value, 4 ); value = 3; memcpy( private_data[2] + COF_PL_CALL_UI, &value, 4 );
	guest->v.fuser3 = 5; guest->v.health = 0; has_phone[0] = 1; private_data[14][COF_CALL_RANG] = 1; private_data[14][COF_CALL_ANSWERED] = 1;
	SV_CoF_MomentsFrame();
	assert( !private_data[2][COF_PL_IN_CALL] && guest->v.fuser3 == 0 );
	memcpy( &value, private_data[2] + COF_PL_HIDEHUD, 4 ); assert( value == 1 );
	memcpy( &value, private_data[2] + COF_PL_BARS, 4 ); assert( value == 0 );
	memcpy( &value, private_data[2] + COF_PL_CALL_UI, 4 ); assert( value == 0 );
	assert( get_handle( 14, COF_CALL_PLAYER ) == host && get_think( 14 ) == BASE + COF_CALL_WAIT && call->v.nextthink > 100.0f );
	assert( !private_data[14][COF_CALL_RANG] && !private_data[14][COF_CALL_ANSWERED] );
	assert( logged( "call-dropped call=sophiecall" ) == 1 && logged( "call-restarted call=sophiecall entity=1" ) == 1 );
	/* Review I-3: the game drops it again at once (it disagrees about the phone):
	 * the next restart waits, and the host is told only once. */
	memset( prompts, 0, sizeof( prompts )); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 0;
	sv.time = 100.2; SV_CoF_MomentsFrame(); assert( logged( "call-restarted" ) == 1 && call->v.nextthink == 0 );
	sv.time = 103.5; SV_CoF_MomentsFrame(); assert( logged( "call-restarted" ) == 2 && !prompts[0][0] && !prompts[1][0] );
	/* Nobody can take it: after 30 s its target fires once and the call is over. */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 0; set_handle( 14, COF_CALL_PLAYER, guest );
	SV_CoF_MomentsFrame(); sv.time = 120; SV_CoF_MomentsFrame(); assert( !fired );
	sv.time = 131; SV_CoF_MomentsFrame(); assert( fired == 1 && !strcmp( fired_name, "gateinter" ) && get_think( 14 ) == 0 );
	sv.time = 140; SV_CoF_MomentsFrame(); assert( fired == 1 && logged( "call-forced call=sophiecall target=gateinter" ) == 1 );
	/* a phone turning up in time restarts it instead */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_WAIT ); call->v.nextthink = 0; set_handle( 14, COF_CALL_PLAYER, guest );
	SV_CoF_MomentsFrame(); sv.time = 110; has_phone[1] = 1; SV_CoF_MomentsFrame();
	assert( !fired && get_think( 14 ) == BASE + COF_CALL_WAIT && logged( "call-restarted" ) == 1 );
	/* a finished call (EndCall cleared its think) is left alone; a call talking normally too */
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, 0 ); SV_CoF_MomentsFrame(); assert( !log_length );
	setup(); call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_TALK ); call->v.nextthink = 103; SV_CoF_MomentsFrame(); assert( !log_length );
	/* anyone dead with a call's lock gets it cleared, call or no call */
	setup(); private_data[2][COF_PL_IN_CALL] = 1; guest->v.health = 0; SV_CoF_MomentsFrame();
	assert( !private_data[2][COF_PL_IN_CALL] && logged( "call-flags-cleared entity=2" ) == 1 );
	setup(); private_data[2][COF_PL_IN_CALL] = 1; SV_CoF_MomentsFrame(); assert( private_data[2][COF_PL_IN_CALL] ); /* alive: their call */

	/* 1.1.9 (S19): a checkpoint loaded in a 911/112 call keeps m_bInPhoneCall with nothing to end it (its step, P+0x3A0,
	 * is not saved): after 2 s with no running call naming them and no step, it is cleared. */
	setup(); private_data[2][COF_PL_IN_CALL] = 1; guest->v.fuser3 = 5.0f; SV_CoF_MomentsFrame();
	sv.time = 101.9; SV_CoF_MomentsFrame(); assert( private_data[2][COF_PL_IN_CALL] );
	sv.time = 102.0; SV_CoF_MomentsFrame(); assert( !private_data[2][COF_PL_IN_CALL] && guest->v.fuser3 == 0.0f && logged( "call-flags-cleared entity=2" ) == 1 );
	setup(); private_data[2][COF_PL_IN_CALL] = 1; value = 2; memcpy( private_data[2] + COF_PL_VAN_CALL, &value, 4 ); /* the van call runs */
	SV_CoF_MomentsFrame(); sv.time = 110; SV_CoF_MomentsFrame(); assert( private_data[2][COF_PL_IN_CALL] );
	setup(); private_data[2][COF_PL_IN_CALL] = 1; call = spawn( 14, 5, 6, 7 ); set_think( 14, COF_CALL_TALK ); call->v.nextthink = 103;
	set_handle( 14, COF_CALL_USER, guest ); SV_CoF_MomentsFrame(); sv.time = 102.5; SV_CoF_MomentsFrame(); assert( private_data[2][COF_PL_IN_CALL] );
	/* A ladder's camera left attached to a player who is not climbing (a loaded save: the field is saved, and InitHUD
	 * attaches the view again after every load and respawn): after 2 s free, the view and the fields are given back. */
	setup(); ladder_camera = attach_camera( 38 );
	frames( 100.0, 101.5 ); assert( !views_given_back[1] && get_handle( 2, COF_PL_CAMERA_HANDLE ) == ladder_camera );
	frames( 102.0, 102.0 );
	assert( views_given_back[1] == 1 && !views_given_back[0] && !get_handle( 2, COF_PL_CAMERA_HANDLE ));
	assert( !word( 2, COF_PL_CAMERA_MODEL ) && ( ladder_camera->v.flags & FL_KILLME ) && !( guest->v.flags & FL_NOTARGET ));
	assert( !word( 15, COF_LADDER_CLIMBER ) && !entities[15].v.owner && logged( "phase=ladder-view-released userid=101" ) == 1 );
	frames( 102.5, 110.0 ); assert( views_given_back[1] == 1 );
	/* nobody owns the ladder or the camera any more (a load): released the same */
	setup(); ladder_camera = attach_camera( 38 ); ladder_camera->v.owner = NULL; entities[15].v.owner = NULL;
	frames( 100.0, 102.0 ); assert( views_given_back[1] == 1 );
	/* a climb freezes its climber: left alone however long; the 2 s count from when they are free */
	setup(); ladder_camera = attach_camera( 38 ); guest->v.flags = FL_FROZEN; entities[15].v.owner = NULL;
	frames( 100.0, 120.0 ); assert( !views_given_back[1] );
	guest->v.flags = 0; frames( 120.5, 122.0 ); assert( !views_given_back[1] );
	guest->v.flags = FL_FROZEN; frames( 122.5, 122.5 ); guest->v.flags = 0; frames( 123.0, 124.5 ); assert( !views_given_back[1] );
	frames( 125.0, 125.0 ); assert( views_given_back[1] == 1 );
	/* the clock going back (a loaded checkpoint) starts the count again */
	setup(); attach_camera( 38 ); frames( 100.0, 101.0 ); frames( 50.0, 51.5 ); assert( !views_given_back[1] );
	/* another camera model (a model cutscene that leaves its players free), a dead player, a companion, the story off */
	setup(); attach_camera( 39 ); frames( 100.0, 110.0 ); assert( !views_given_back[1] );
	setup(); attach_camera( 38 ); guest->v.health = 0; frames( 100.0, 110.0 ); assert( !views_given_back[1] );
	setup(); attach_camera( 38 ); clients[1].flags = FCL_FAKECLIENT; frames( 100.0, 110.0 ); assert( !views_given_back[1] );
	setup(); attach_camera( 38 ); gate = 0; frames( 100.0, 110.0 ); assert( !views_given_back[1] ); gate = 1;

	/* Ladders: while the host climbs, nobody near its use brushes can start it. The climb freezes the climber and
	 * hands them the ladder's camera model (P+0x5AC); its FL_GODMODE lasts one frame (PostThink clears it). */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = host; manager->v.model = 38; host->v.flags = FL_FROZEN;
	value = 0x5678; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 );
	ladder_camera = spawn( 18, 12, 0, 0 ); ladder_camera->v.owner = host; ladder_camera->v.model = 38;
	use = spawn( 16, 9, 0, 10 ); use->v.absmin[0] = 480; use->v.absmax[0] = 520; use->v.absmax[1] = use->v.absmax[2] = 10;
	SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE | 1; SV_CoF_MomentsCommand( &clients[1], &cmd );
	assert( cmd.buttons == 1 && !strcmp( prompts[1], "5:Little Rascal 1 is climbing. Wait a moment." ) && logged( "ladder-busy userid=101 climber=100" ) == 1 );
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == 0 && logged( "ladder-busy" ) == 1 ); /* told once */
	/* far away, or the climb over (LadderThink gives control back and removes the camera model in one think; 1.1.9:
	 * control back alone is a camera's end, which S13 undoes): +use goes through */
	guest->v.origin[0] = 900; cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	guest->v.origin[0] = 500; host->v.flags = 0; value = 0; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 ); SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	/* frozen for something else (no ladder camera): not a climb */
	host->v.flags = FL_FROZEN; value = 0; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 ); SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	/* 1.2.6: held by a strangle or a model cutscene (another camera model; the manager still names its last climber):
	 * the ladder is free */
	value = 0x5678; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 ); ladder_camera->v.model = 39; SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	ladder_camera->v.model = 38; SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == 0 ); /* its own model: busy */
	/* another ladder's brush is not this one's */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = host; host->v.flags = FL_FROZEN;
	value = 0x5678; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 );
	use = spawn( 16, 9, 0, 17 ); use->v.absmin[0] = 480; use->v.absmax[0] = 520; SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );

	/* 1.1.9: the climb reads forward and back from the ladder's camera model (LadderThink); the usercmd's own
	 * keys reach it now, before a frozen player's buttons are dropped. A key held since the climb began counts once
	 * let go (as the client's forwardpressed/backpressed commands); the model's other buttons are kept. */
	{
		edict_t *camera;
		setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = host; manager->v.model = 38; host->v.flags = FL_FROZEN;
		value = 0x5678; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 );
		camera = spawn( 18, 12, 0, 0 ); camera->v.owner = host; camera->v.model = 38; camera->v.button = 1 << 11;
		SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD ); assert( camera->v.button == 1 << 11 ); /* held from before */
		SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD ); assert( camera->v.button == 1 << 11 );
		SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( camera->v.button == 1 << 11 );
		SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD | IN_USE ); assert( camera->v.button == (int)( IN_FORWARD | ( 1 << 11 )));
		SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD ); assert( camera->v.button == (int)( IN_FORWARD | ( 1 << 11 ))); /* held */
		SV_CoF_MomentsLadderInput( &clients[0], IN_BACK ); assert( camera->v.button == (int)( IN_BACK | ( 1 << 11 ))); /* up at the start */
		camera->v.button |= IN_FORWARD; /* a forwardpressed that came late, after the key was let go */
		SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( camera->v.button == 1 << 11 );
		/* the partner's keys are theirs */
		SV_CoF_MomentsLadderInput( &clients[1], IN_FORWARD ); assert( camera->v.button == 1 << 11 );
		/* a new climb (another model): keys held from before wait again */
		camera->free = 1; camera = spawn( 19, 12, 0, 0 ); camera->v.owner = host; camera->v.model = 38;
		SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD ); assert( !camera->v.button );
		SV_CoF_MomentsLadderInput( &clients[0], 0 ); SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD );
		assert( camera->v.button == (int)IN_FORWARD );
		/* not a ladder's model (a strangle, a model cutscene; the old ladder still names the player): untouched */
		camera->v.model = 39; camera->v.button = 0; SV_CoF_MomentsLadderInput( &clients[0], 0 ); SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD );
		assert( !camera->v.button );
		camera->v.model = 38; camera->v.button = (int)IN_FORWARD;
		/* not climbing (unfrozen, or no camera model in the player's data), no story co-op, an AI companion: untouched */
		host->v.flags = 0; SV_CoF_MomentsLadderInput( &clients[0], IN_FORWARD ); SV_CoF_MomentsLadderInput( &clients[0], 0 );
		assert( camera->v.button == (int)IN_FORWARD );
		host->v.flags = FL_FROZEN; value = 0; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 );
		SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( camera->v.button == (int)IN_FORWARD );
		value = 0x5678; memcpy( private_data[1] + COF_PL_CAMERA_MODEL, &value, 4 );
		gate = 0; SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( camera->v.button == (int)IN_FORWARD ); gate = 1;
		clients[0].flags = FCL_FAKECLIENT; SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( camera->v.button == (int)IN_FORWARD );
		clients[0].flags = 0; SV_CoF_MomentsLadderInput( &clients[0], 0 ); assert( !camera->v.button );
	}

	/* The wheelchair's rider is the first player while it carries them. */
	setup(); chair = spawn( 17, 11, 0, 0 ); chair->v.owner = guest; value = 0x1234; memcpy( private_data[17] + COF_CHAIR_RIDER, &value, 4 );
	SV_CoF_MomentsFrame(); assert( preferred == guest );
	value = 0; memcpy( private_data[17] + COF_CHAIR_RIDER, &value, 4 ); SV_CoF_MomentsFrame(); assert( preferred == NULL );

	/* A player who leaves: no ladder or wheelchair keeps them; their camera model
	 * stays (their player data points at it and a model cutscene walks it). */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = guest; value = 0x5678; memcpy( private_data[15] + COF_LADDER_CLIMBER, &value, 4 );
	chair = spawn( 17, 11, 0, 0 ); chair->v.owner = guest; memcpy( private_data[17] + COF_CHAIR_RIDER, &value, 4 );
	spawn( 18, 12, 0, 0 )->v.owner = guest; spawn( 19, 12, 0, 0 )->v.owner = host;
	SV_CoF_MomentsDrop( &clients[1] );
	memcpy( &value, private_data[15] + COF_LADDER_CLIMBER, 4 ); assert( !value && !manager->v.owner );
	memcpy( &value, private_data[17] + COF_CHAIR_RIDER, 4 ); assert( !value && ( chair->v.flags & FL_KILLME ));
	assert(!( entities[18].v.flags & FL_KILLME ) && !( entities[19].v.flags & FL_KILLME ));
	assert( logged( "ladder-released userid=101" ) == 1 && logged( "wheelchair-removed userid=101" ) == 1 );

	/* Review I-2: the climber leaves mid-climb: their camera model is let go and removed,
	 * so later spawns are not held in the model cutscene wait for it. */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = guest; guest->v.flags = FL_FROZEN;
	value = 0x5678; memcpy( private_data[15] + COF_LADDER_CLIMBER, &value, 4 ); memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &value, 4 );
	set_handle( 2, COF_PL_CAMERA_HANDLE, &entities[18] ); spawn( 18, 12, 0, 0 )->v.owner = guest;
	SV_CoF_MomentsDrop( &clients[1] );
	memcpy( &value, private_data[2] + COF_PL_CAMERA_MODEL, 4 ); assert( !value && !get_handle( 2, COF_PL_CAMERA_HANDLE ));
	assert( entities[18].v.flags & FL_KILLME );
	/* Yes/no: only who was asked answers, the first answer wins, a new question reopens it. */
	setup(); spawn( 20, 13, 15, 0 ); yesno_message( 0, 20 );
	assert( command( 1, "yesnoactivate", "20" ) && !strcmp( prompts[1], "5:Someone else was asked that." ));
	assert( !command( 0, "yesnoactivate", "20" ) && logged( "yesno-answer userid=100 entity=20 command=yesnoactivate" ) == 1 );
	assert( command( 0, "yesnono", "20" ) && !strcmp( prompts[0], "5:That has already been answered." ));
	yesno_message( 1, 20 ); assert( command( 1, "yesnono", "20" )); /* a "yes" is final: asking again does not reopen */
	/* after a "no", asking again does reopen it */
	setup(); spawn( 20, 13, 15, 0 ); yesno_message( 0, 20 ); assert( !command( 0, "yesnono", "20" ));
	yesno_message( 1, 20 ); assert( !command( 1, "yesnoactivate", "20" ));
	/* both asked (c_roofs2): the first answer wins; the other is released from its mark */
	setup(); spawn( 20, 13, 14, 0 ); yesno_message( 0, 20 ); yesno_message( 1, 20 ); guest->v.fuser3 = 1.0f;
	assert( !command( 0, "yesnoactivate", "20" ));
	assert( command( 1, "yesnoactivate", "20" ) && guest->v.fuser3 == 0.0f && logged( "yesno-refused userid=101" ) == 1 );
	/* review minor 9: the roof trigger asks the partner again 5 s later: still answered */
	yesno_message( 1, 20 ); guest->v.fuser3 = 1.0f;
	assert( command( 1, "yesnoactivate", "20" ) && guest->v.fuser3 == 0.0f );
	/* a question the story layer never saw, another command, or the layer off: the game decides */
	setup(); spawn( 20, 13, 15, 0 ); assert( !command( 1, "yesnoactivate", "20" ));
	yesno_message( 0, 20 ); assert( !command( 1, "say", "20" ));
	gate = 0; assert( !command( 1, "yesnoactivate", "20" ));

	/* The intro's logos: the one the game showed the host reaches the guest the next
	 * frame, once; the copy is not copied again; nothing for a fake client. */
	{
		byte logo = 3;
		setup(); SV_CoF_MomentsFrame();
		SV_CoF_MomentsMessage( clients[0].edict, "CoFLogo", &logo, 1 );
		assert( !logos_to[1] );
		SV_CoF_MomentsFrame();
		assert( logos_to[1] == 1 && !logos_to[0] && logo_value == 3 && logged( "logo-shared from=100 logo=3" ) == 1 );
		SV_CoF_MomentsFrame(); assert( logos_to[1] == 1 );
		setup(); SV_CoF_MomentsFrame(); clients[1].flags = FCL_FAKECLIENT;
		SV_CoF_MomentsMessage( clients[0].edict, "CoFLogo", &logo, 1 ); SV_CoF_MomentsFrame();
		assert( !logos_to[1] && !logos_to[0] );
		/* story co-op off: the game's own message only */
		setup(); gate = 0; SV_CoF_MomentsMessage( clients[0].edict, "CoFLogo", &logo, 1 ); gate = 1; SV_CoF_MomentsFrame();
		assert( !logos_to[1] );
	}

	/* a phone in slot 10 rings like one in slot 3 */
	has_phone[0] = 2; assert( CoF_MomentHasPhone( &clients[0] ));
	has_phone[0] = 0; assert( !CoF_MomentHasPhone( &clients[0] ));

	/* Two players stacked on one spot (c_intro's last room, kunitei, fits one: the party
	 * shares the owner's spot). Retail MP PreThink makes a player SOLID_NOT while it
	 * overlaps another, and the engine skips trigger touches for SOLID_NOT bodies: the
	 * exit there never fired while an idle companion stood on the player (live 2026-10-15).
	 * A living stacked player still stands in its triggers. */
	setup();
	{
		vec3_t lo = { -16, -16, -36 }, hi = { 16, 16, 36 };
		VectorCopy( lo, host->v.absmin ); VectorCopy( hi, host->v.absmax );
		VectorCopy( lo, guest->v.absmin ); VectorCopy( hi, guest->v.absmax );
	}
	host->v.solid = guest->v.solid = SOLID_NOT;
	assert( SV_CoF_MomentsStackedTouch( host ) && SV_CoF_MomentsStackedTouch( guest ));
	host->v.solid = SOLID_SLIDEBOX; assert( !SV_CoF_MomentsStackedTouch( host )); host->v.solid = SOLID_NOT;
	guest->v.absmin[0] += 40; guest->v.absmax[0] += 40;           /* apart: SOLID_NOT for another reason */
	assert( !SV_CoF_MomentsStackedTouch( host ));
	guest->v.absmin[0] -= 40; guest->v.absmax[0] -= 40;
	guest->v.health = 0; assert( !SV_CoF_MomentsStackedTouch( host )); /* a body is no partner */
	guest->v.health = 100; host->v.deadflag = 1; assert( !SV_CoF_MomentsStackedTouch( host )); /* the dead touch nothing */
	host->v.deadflag = DEAD_NO;
	gate = 0; assert( !SV_CoF_MomentsStackedTouch( host )); gate = 1;
	assert( !SV_CoF_MomentsStackedTouch( &entities[5] ) && !SV_CoF_MomentsStackedTouch( NULL ));
	/* 1.1.8 (M1 findings 2 and 6): a trigger_once that teleports its toucher into a kill box is a fall floor:
	 * only who touched it dies, and it stays for the next. */
	setup(); fall_trap(); SV_CoF_MomentsFrame();
	assert( logged( "phase=fall-floor trigger=10 target=multifallheh trap=1" ) == 1 );
	assert( SV_CoF_MomentsTriggerTouch( &entities[10], guest ) && kills[2] == 1 && !kills[1] );
	memcpy( &bits, private_data[2] + COF_PL_DEATHBITS, 4 ); assert( bits == 0 );
	assert( SV_CoF_MomentsTriggerTouch( &entities[10], host ) && kills[1] == 1 && logged( "fall-death userid=100 trigger=10" ) == 1 );
	/* c_city's flowers: the hurt starts off and the same chain switches it on */
	{
		string_t chain[] = { 22, 24 };
		setup(); fall_trap(); mm_spawn( 11, 21, 2, chain ); entities[14].v.solid = SOLID_NOT; entities[14].v.targetname = 24;
		SV_CoF_MomentsFrame(); assert( logged( "trap=1" ) == 1 );
		/* ...a hurt the chain switches off is no trap */
		setup(); fall_trap(); mm_spawn( 11, 21, 2, chain ); entities[14].v.targetname = 24;
		SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ) && !SV_CoF_MomentsTriggerTouch( &entities[10], guest ));
	}
	/* no trap: a hurt that is off and stays so, one that spares players (8), one too weak, a landing beside the
	 * brush's own shape (in its box only), a teleport that moves a monster, a manager not laid out as expected */
	setup(); fall_trap(); entities[14].v.solid = SOLID_NOT; SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	setup(); fall_trap(); entities[14].v.spawnflags = 8; SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	setup(); fall_trap(); entities[14].v.dmg = 50; SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	setup(); fall_trap(); set_vec( models[1].shape_min, 1800, 400, 300 ); set_vec( models[1].shape_max, 1850, 450, 320 );
	SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	setup(); fall_trap(); entities[12].v.message = 29; SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	setup(); fall_trap(); value = 99; memcpy( private_data[11] + COF_MM_COUNT, &value, 4 ); SV_CoF_MomentsFrame(); assert( !logged( "fall-floor" ));
	/* the trigger aimed straight at the teleport counts too */
	setup(); fall_trap(); entities[10].v.target = 22; SV_CoF_MomentsFrame(); assert( logged( "target=teleportfall trap=1" ) == 1 );

	/* 1.1.8 (M1 finding 4): c_roofs has two entrances to its 358 s scene (multiroofscene; rsstart fires it 0.5 s
	 * later). The first that fires switches the other off, which would replay the scene and toggle stater off. */
	{
		string_t scene[] = { 27 }, start[] = { 25 };
		setup(); spawn( 10, 2, 0, 25 ); mm_spawn( 11, 25, 1, scene ); spawn( 12, 2, 0, 26 ); mm_spawn( 13, 26, 1, start );
		SV_CoF_MomentsFrame();
		assert( logged( "scene-entrance trigger=10 target=multiroofscene" ) == 1 && logged( "scene-entrance trigger=12 target=rsstart" ) == 1 );
		assert( !entities[10].v.iuser1 && !entities[12].v.iuser1 );
		entities[10].v.flags |= FL_KILLME; SV_CoF_MomentsFrame();
		assert( entities[12].v.iuser1 == 1 && logged( "scene-entrance-off trigger=12 fired=10" ) == 1 );
		SV_CoF_MomentsFrame(); assert( logged( "scene-entrance-off" ) == 1 );
		/* the other way round: the entrance is gone (freed) */
		setup(); spawn( 10, 2, 0, 25 ); mm_spawn( 11, 25, 1, scene ); spawn( 12, 2, 0, 26 ); mm_spawn( 13, 26, 1, start );
		SV_CoF_MomentsFrame(); entities[12].free = 1; SV_CoF_MomentsFrame();
		assert( entities[10].v.iuser1 == 1 && logged( "scene-entrance-off trigger=10 fired=12" ) == 1 );
		/* c_nightmare: two aimed at one manager, which fires the third's: any one switches the other two off */
		setup(); spawn( 10, 2, 0, 26 ); spawn( 14, 2, 0, 26 ); mm_spawn( 13, 26, 1, start ); spawn( 12, 2, 0, 25 ); mm_spawn( 11, 25, 1, scene );
		SV_CoF_MomentsFrame(); entities[12].v.flags |= FL_KILLME; SV_CoF_MomentsFrame();
		assert( entities[10].v.iuser1 == 1 && entities[14].v.iuser1 == 1 && logged( "scene-entrance-off" ) == 2 );
		/* a named trigger (the map may switch or remove it) or an entrance to another scene: left alone */
		setup(); spawn( 10, 2, 0, 25 ); mm_spawn( 11, 25, 1, scene ); spawn( 12, 2, 24, 26 ); mm_spawn( 13, 26, 1, start );
		SV_CoF_MomentsFrame(); assert( !logged( "scene-entrance" ));
		setup(); spawn( 10, 2, 0, 25 ); mm_spawn( 11, 25, 1, scene ); spawn( 12, 2, 0, 26 ); mm_spawn( 13, 26, 1, scene );
		SV_CoF_MomentsFrame(); entities[10].free = 1; SV_CoF_MomentsFrame(); assert( !logged( "scene-entrance" ) && !entities[12].v.iuser1 );
	}

	/* 1.1.8 (K6): the c_lake boat's rider leaves mid-crossing: the boat is freed and goes back to where the map put it */
	{
		edict_t *boat;
		uint32_t rider = (uint32_t)(uintptr_t)private_data[2], got;
		setup(); boat = spawn( 15, 28, 0, 0 ); set_vec( boat->v.origin, 594, -2862, 9 ); set_vec( boat->v.angles, 0, -10, 0 );
		SV_CoF_MomentsFrame();
		set_vec( boat->v.origin, -300, 2000, 9 ); boat->v.angles[1] = 80; set_vec( boat->v.velocity, 50, 0, 0 );
		memcpy( private_data[15] + COF_BOAT_RIDER, &rider, 4 ); memcpy( private_data[15] + COF_BOAT_EXIT, &rider, 4 );
		memcpy( private_data[15] + COF_BOAT_TELLER, &rider, 4 ); set_handle( 2, COF_PL_BOAT, boat );
		SV_CoF_MomentsDrop( &clients[0] ); assert( !links ); /* the host is not its rider */
		SV_CoF_MomentsDrop( &clients[1] );
		memcpy( &got, private_data[15] + COF_BOAT_RIDER, 4 ); assert( !got );
		memcpy( &got, private_data[15] + COF_BOAT_EXIT, 4 ); assert( !got );
		memcpy( &got, private_data[15] + COF_BOAT_TELLER, 4 ); assert( !got );
		assert( boat->v.origin[0] == 594 && boat->v.origin[1] == -2862 && boat->v.angles[1] == -10 && !boat->v.velocity[0] && links == 1 );
		assert( !get_handle( 2, COF_PL_BOAT ) && logged( "boat-freed userid=101 boat=15 moved=1" ) == 1 );
		/* Before a level change every rider is let go as Disembark does, without the move: on board (m_pBoat), out of
		 * the boat's view with the health HUD back; still boarding (the 1 s fade), only control and the fade. */
		setup(); boat = spawn( 15, 28, 0, 0 ); memcpy( private_data[15] + COF_BOAT_RIDER, &rider, 4 ); set_handle( 2, COF_PL_BOAT, boat );
		value = 8 | 1; memcpy( private_data[2] + COF_PL_HIDEHUD, &value, 4 ); set_vec( boat->v.origin, 10, 20, 0 );
		SV_CoF_MomentsReleaseRiders();
		memcpy( &got, private_data[15] + COF_BOAT_RIDER, 4 ); memcpy( &value, private_data[2] + COF_PL_HIDEHUD, 4 );
		assert( !got && !get_handle( 2, COF_PL_BOAT ) && value == 1 && boats_to[1] == 1 && !boats_to[0] );
		assert( controls_given[2] == 1 && faded_in[1] == 1 && boat->v.origin[0] == 10 && logged( "boat-released boat=15 userid=101" ) == 1 );
		SV_CoF_MomentsReleaseRiders(); assert( controls_given[2] == 1 ); /* nobody rides now */
		setup(); boat = spawn( 15, 28, 0, 0 ); memcpy( private_data[15] + COF_BOAT_RIDER, &rider, 4 );
		value = 8; memcpy( private_data[2] + COF_PL_HIDEHUD, &value, 4 ); SV_CoF_MomentsReleaseRiders();
		memcpy( &value, private_data[2] + COF_PL_HIDEHUD, 4 ); assert( value == 8 && !boats_to[1] && controls_given[2] == 1 && faded_in[1] == 1 );
	}

	/* 1.1.8 (M3 finding 7): c_ending4's cof_greyfade shows the grey fade to the host only: the guest gets it the
	 * next frame, once; a fade the game sent everyone (the book Simons) is not sent again. */
	{
		byte fade[2] = { 0, 3 };
		setup(); SV_CoF_MomentsFrame(); SV_CoF_MomentsMessage( host, "GreyFade", fade, 2 ); assert( !fades_to[1] );
		SV_CoF_MomentsFrame(); assert( fades_to[1] == 1 && !fades_to[0] && fade_value[1] == 3 && logged( "greyfade-shared in=0 seconds=3 to=1" ) == 1 );
		SV_CoF_MomentsFrame(); assert( fades_to[1] == 1 );
		setup(); SV_CoF_MomentsFrame(); SV_CoF_MomentsMessage( host, "GreyFade", fade, 2 ); SV_CoF_MomentsMessage( guest, "GreyFade", fade, 2 );
		SV_CoF_MomentsFrame(); assert( !fades_to[0] && !fades_to[1] && !logged( "greyfade-shared" ));
		setup(); SV_CoF_MomentsFrame(); clients[1].flags = FCL_FAKECLIENT; SV_CoF_MomentsMessage( host, "GreyFade", fade, 2 );
		SV_CoF_MomentsFrame(); assert( !fades_to[1] );
		setup(); gate = 0; SV_CoF_MomentsMessage( host, "GreyFade", fade, 2 ); gate = 1; SV_CoF_MomentsFrame(); assert( !fades_to[1] );
	}

	/* 1.1.8 (M3 finding 6d): after cof_stopwheelchair the empty chair would board anyone and take their weapons:
	 * it takes nobody now (no use function). A carried chair, or a dead one (the rescue's), keeps it. */
	{
		uint32_t chair_use = BASE + 0x86000, got, carried = 0x1234;
		setup(); chair = spawn( 17, 11, 0, 0 ); chair->v.owner = guest; memcpy( private_data[17] + COF_USE, &chair_use, 4 );
		SV_CoF_MomentsFrame(); memcpy( &got, private_data[17] + COF_USE, 4 );
		assert( !got && logged( "wheelchair-stopped chair=17" ) == 1 && preferred == NULL );
		SV_CoF_MomentsFrame(); assert( logged( "wheelchair-stopped" ) == 1 );
		setup(); chair = spawn( 17, 11, 0, 0 ); chair->v.owner = guest; memcpy( private_data[17] + COF_USE, &chair_use, 4 );
		memcpy( private_data[17] + COF_CHAIR_RIDER, &carried, 4 ); SV_CoF_MomentsFrame();
		memcpy( &got, private_data[17] + COF_USE, 4 ); assert( got == chair_use && preferred == guest );
		setup(); chair = spawn( 17, 11, 0, 0 ); memcpy( private_data[17] + COF_USE, &chair_use, 4 ); private_data[17][COF_CHAIR_DEAD] = 1;
		SV_CoF_MomentsFrame(); memcpy( &got, private_data[17] + COF_USE, 4 ); assert( got == chair_use );
	}
	/* 1.1.8 (M1 finding 8): c_down's trigger_relay killladddd would remove the ladder's only use (ladddd) under a
	 * partner still at the top: its kill of a ladder use is dropped; a relay killing anything else keeps its kill. */
	{
		string_t kill = 31, got;
		setup(); spawn( 16, 9, 31, 10 ); spawn( 20, 30, 0, 0 ); memcpy( private_data[20] + COF_DELAY_KILLTARGET, &kill, 4 );
		kill = 22; spawn( 21, 30, 0, 0 ); memcpy( private_data[21] + COF_DELAY_KILLTARGET, &kill, 4 ); spawn( 22, 18, 22, 0 );
		SV_CoF_MomentsFrame();
		memcpy( &got, private_data[20] + COF_DELAY_KILLTARGET, 4 ); assert( !got && logged( "ladder-kept relay=20 use=ladddd" ) == 1 );
		memcpy( &got, private_data[21] + COF_DELAY_KILLTARGET, 4 ); assert( got == 22 && logged( "ladder-kept" ) == 1 );
	}
	/* 1.1.9 (S2): c_ending1's house: trigger_once #259 -> multibokam0 -> multibokam -> multisu -> teledes_tele (a
	 * cof_teleport moving only a player activator) 27 s later. A multi_manager keeps the activator (+0x184), a delayed
	 * trigger's DelayedUse too (+0xE4): one naming the player who leaves now names the living person nearest them. */
	{
		string_t chain[] = { 22 };
		float delay = 0.5f, early = -5.0f;
		int two = 2;
		edict_t *third = &entities[3];
		setup(); svs.maxclients = 3; clients[2].state = cs_spawned; clients[2].userid = 102; clients[2].edict = third;
		third->free = 0; third->v.classname = 1; third->v.health = 100; third->v.origin[0] = 450;
		mm_spawn( 11, 21, 1, chain ); set_handle( 11, COF_MM_ACTIVATOR, guest );
		mm_spawn( 12, 25, 1, chain ); set_handle( 12, COF_MM_ACTIVATOR, host );          /* the host's: kept */
		spawn( 13, 32, 0, 22 ); set_handle( 13, COF_DELAY_ACTIVATOR, guest );
		spawn( 14, 32, 0, 22 ); set_handle( 14, COF_DELAY_ACTIVATOR, guest ); memcpy( private_data[14] + COF_DELAY_DELAY, &delay, 4 );
		mm_spawn( 15, 26, 1, chain ); set_handle( 15, COF_MM_ACTIVATOR, guest );
		memcpy( private_data[15] + COF_MM_COUNT, &two, 4 ); memcpy( private_data[15] + COF_MM_DELAYS + 4, &early, 4 );
		spawn( 16, 30, 0, 22 ); set_handle( 16, COF_DELAY_ACTIVATOR, guest );              /* a trigger_relay keeps none */
		SV_CoF_MomentsDrop( &clients[1] );
		assert( get_handle( 11, COF_MM_ACTIVATOR ) == third && get_handle( 12, COF_MM_ACTIVATOR ) == host );
		assert( get_handle( 13, COF_DELAY_ACTIVATOR ) == third );
		assert( get_handle( 14, COF_DELAY_ACTIVATOR ) == guest );   /* not one SUB_UseTargets made (its delay is 0) */
		assert( get_handle( 15, COF_MM_ACTIVATOR ) == guest );      /* delays out of order: not the layout read here */
		assert( get_handle( 16, COF_DELAY_ACTIVATOR ) == guest );
		assert( logged( "activator-handed userid=101 entity=11 class=multi_manager to=3" ) == 1 );
		assert( logged( "activator-handed userid=101 entity=13 class=DelayedUse to=3" ) == 1 && logged( "activator-handed" ) == 2 );
		/* the nearest is dead, or a companion: the host; nobody alive: left as it was; a stale handle is not the leaver */
		set_handle( 11, COF_MM_ACTIVATOR, guest ); third->v.health = 0; SV_CoF_MomentsDrop( &clients[1] );
		assert( get_handle( 11, COF_MM_ACTIVATOR ) == host );
		set_handle( 11, COF_MM_ACTIVATOR, guest ); third->v.health = 100; clients[2].flags = FCL_FAKECLIENT; SV_CoF_MomentsDrop( &clients[1] );
		assert( get_handle( 11, COF_MM_ACTIVATOR ) == host );
		set_handle( 11, COF_MM_ACTIVATOR, guest ); host->v.health = 0; SV_CoF_MomentsDrop( &clients[1] );
		assert( get_handle( 11, COF_MM_ACTIVATOR ) == guest );
		host->v.health = 100; set_handle( 11, COF_MM_ACTIVATOR, guest ); guest->serialnumber = 2; SV_CoF_MomentsDrop( &clients[1] );
		assert( get_handle( 11, COF_MM_ACTIVATOR ) == guest );
		guest->serialnumber = 1; gate = 0; SV_CoF_MomentsDrop( &clients[1] ); assert( get_handle( 11, COF_MM_ACTIVATOR ) == guest ); gate = 1;
	}
	/* 1.1.9 (P2, M2 finding 8): c_bridge: the trigger_once by the carriage starts destroyall -> multikuken2, which
	 * switches the trigger_hurt tjenahaha (1000) on under the carriage 3 s later: a partner still inside is set down
	 * beside the nearest living person outside it, before any move of theirs touches it. */
	{
		string_t hurts[] = { 24 };
		edict_t *hurt;
		setup(); mm_spawn( 11, 25, 1, hurts ); set_handle( 11, COF_MM_ACTIVATOR, host );
		hurt = spawn( 14, 20, 24, 0 ); hurt->v.dmg = 1000; hurt->v.solid = SOLID_NOT; hurt->v.modelindex = 1;
		models[1].type = mod_brush; models[1].hulls[1].firstclipnode = 1; set_vec( models[1].hulls[1].clip_mins, -16, -16, -36 );
		set_vec( models[1].shape_min, 1800, 400, 300 ); set_vec( models[1].shape_max, 2000, 600, 450 );
		set_vec( hurt->v.absmin, 1800, 400, 300 ); set_vec( hurt->v.absmax, 2000, 600, 450 );
		set_vec( guest->v.origin, 1900, 500, 350 ); set_vec( host->v.origin, 1000, 500, 350 );
		SV_CoF_MomentsFrame(); SV_CoF_MomentsFrame(); assert( !section_moves );            /* off: nothing */
		hurt->v.solid = SOLID_TRIGGER; SV_CoF_MomentsFrame();
		assert( section_moves == 1 && guest->v.origin[0] == 1035.0f && logged( "hurt-escaped userid=101 hurt=14 anchor=100 ok=1" ) == 1 );
		set_vec( guest->v.origin, 1900, 500, 350 ); SV_CoF_MomentsFrame(); assert( section_moves == 1 ); /* only as it goes on */
		hurt->v.solid = SOLID_NOT; SV_CoF_MomentsFrame(); hurt->v.solid = SOLID_TRIGGER; SV_CoF_MomentsFrame(); assert( section_moves == 2 );
		/* the starter inside it (a trap's single-player death), a dead starter, no manager behind it, or one already
		 * on when the map started: as retail */
		set_vec( guest->v.origin, 1900, 500, 350 ); set_vec( host->v.origin, 1950, 500, 350 );
		hurt->v.solid = SOLID_NOT; SV_CoF_MomentsFrame(); hurt->v.solid = SOLID_TRIGGER; SV_CoF_MomentsFrame(); assert( section_moves == 2 );
		set_vec( host->v.origin, 1000, 500, 350 ); host->v.health = 0;
		hurt->v.solid = SOLID_NOT; SV_CoF_MomentsFrame(); hurt->v.solid = SOLID_TRIGGER; SV_CoF_MomentsFrame(); assert( section_moves == 2 );
		host->v.health = 100; entities[11].free = 1;
		hurt->v.solid = SOLID_NOT; SV_CoF_MomentsFrame(); hurt->v.solid = SOLID_TRIGGER; SV_CoF_MomentsFrame(); assert( section_moves == 2 );
		entities[11].free = 0; svs.spawncount++; SV_CoF_MomentsFrame(); assert( section_moves == 2 );
	}
	/* 1.1.9 (S11): a player who respawns in a boss fight gets the bar back from a partner (Spawn cleared P+0x5F4, the
	 * respawn's InitHUD sent BossBar 0), while the partner's boss lives. */
	{
		string_t empty = 33, full = 34, boss_name = 35;
		int max = 1000, health = 640;
		edict_t *boss;
		static const byte expected[] = { 1, 'g','f','x','/','v','g','u','i','/','b','o','s','s','/','s','i','m','o','n','_','h','e','a','l','t','h','_','e','m','p','t','y','.','t','g','a',0,
			'g','f','x','/','v','g','u','i','/','b','o','s','s','/','s','i','m','o','n','_','h','e','a','l','t','h','_','f','u','l','l','.','t','g','a',0, 640 & 0xFF, 640 >> 8, 1000 & 0xFF, 1000 >> 8 };
		setup(); boss = spawn( 10, 29, 35, 0 ); boss->v.health = 640;
		private_data[1][COF_PL_BOSS_BAR] = 1; memcpy( private_data[1] + COF_PL_BOSS_EMPTY, &empty, 4 ); memcpy( private_data[1] + COF_PL_BOSS_FULL, &full, 4 );
		memcpy( private_data[1] + COF_PL_BOSS_MAX, &max, 4 ); memcpy( private_data[1] + COF_PL_BOSS_HEALTH, &health, 4 );
		memcpy( private_data[1] + COF_PL_BOSS_NAME, &boss_name, 4 );
		memcpy( private_data[2] + COF_PL_BOSS_NAME, &boss_name, 4 ); /* the guest's name stays; Spawn clears only the flag */
		SV_CoF_MomentsFrame(); guest->v.health = 0; SV_CoF_MomentsFrame(); assert( !bossbars_to[1] );
		guest->v.health = 100; SV_CoF_MomentsFrame();
		assert( bossbars_to[1] == 1 && !bossbars_to[0] && private_data[2][COF_PL_BOSS_BAR] == 1 && logged( "bossbar-restored userid=101" ) == 1 );
		assert( bossbar_size == (int)sizeof( expected ) && !memcmp( bossbar, expected, sizeof( expected )));
		memcpy( &value, private_data[2] + COF_PL_BOSS_MAX, 4 ); assert( value == 1000 );
		SV_CoF_MomentsFrame(); assert( bossbars_to[1] == 1 ); /* once */
		/* the boss dead (or no partner with a bar), a player who still has it, a fresh joiner: nothing */
		private_data[2][COF_PL_BOSS_BAR] = 0; guest->v.health = 0; SV_CoF_MomentsFrame(); boss->v.health = 0; guest->v.health = 100;
		SV_CoF_MomentsFrame(); assert( bossbars_to[1] == 1 && !private_data[2][COF_PL_BOSS_BAR] );
		boss->v.health = 640; private_data[2][COF_PL_BOSS_BAR] = 1; guest->v.health = 0; SV_CoF_MomentsFrame(); guest->v.health = 100;
		SV_CoF_MomentsFrame(); assert( bossbars_to[1] == 1 );
		private_data[2][COF_PL_BOSS_BAR] = 0; guest->v.health = 0; SV_CoF_MomentsFrame(); clients[1].userid = 111; guest->v.health = 100;
		SV_CoF_MomentsFrame(); assert( bossbars_to[1] == 1 );
	}
	/* 1.1.9 (S18): cof_credits sends Credits to the players there, once: a person who joins during the ending gets a
	 * copy of the first player's last one (with its Continue); the game's own copies are not sent again. */
	{
		byte scroll[8] = { 1, '/', 'a', 0, 9, 9, 9, 1 }, stats[8] = { 0, '/', 'b', 0, 7, 7, 7, 0 }, other[8] = { 0, '/', 'b', 0, 5, 5, 5, 0 };
		edict_t *third = &entities[3];
		setup(); SV_CoF_MomentsFrame();
		SV_CoF_MomentsMessage( host, "Credits", scroll, 8 ); SV_CoF_MomentsMessage( guest, "Credits", scroll, 8 );
		SV_CoF_MomentsFrame(); assert( !credits_to[0] && !credits_to[1] && !logged( "credits-resent" ));
		/* a second Use in the same frame: the last one is kept */
		SV_CoF_MomentsMessage( host, "Credits", stats, 8 ); SV_CoF_MomentsMessage( guest, "Credits", other, 8 );
		svs.maxclients = 3; clients[2].state = cs_spawned; clients[2].userid = 102; clients[2].edict = third;
		third->free = 0; third->v.classname = 1; third->v.health = 100;
		SV_CoF_MomentsFrame();
		assert( credits_to[2] == 1 && !credits_to[0] && !credits_to[1] && credits_size == 8 && !memcmp( credits, stats, 8 ));
		assert( logged( "credits-resent userid=102" ) == 1 );
		SV_CoF_MomentsFrame(); assert( credits_to[2] == 1 );                                   /* once */
		clients[2].flags = FCL_FAKECLIENT; clients[2].userid = 103; SV_CoF_MomentsFrame(); assert( credits_to[2] == 1 );
		/* none shown in this map, or the story layer off: nothing */
		setup(); svs.maxclients = 3; clients[2].state = cs_spawned; clients[2].userid = 102; clients[2].edict = third;
		third->free = 0; third->v.classname = 1; third->v.health = 100; SV_CoF_MomentsFrame(); assert( !credits_to[2] );
		gate = 0; SV_CoF_MomentsMessage( host, "Credits", stats, 8 ); gate = 1; SV_CoF_MomentsFrame(); assert( !credits_to[2] );
		SV_CoF_MomentsMessage( host, "Credits", stats, 8 ); svs.spawncount++; SV_CoF_MomentsFrame(); assert( !credits_to[2] );
	}
	/* 1.1.9 (S13): a camera's or model cutscene's end gives control back to every living player: a climber in the
	 * ladder's camera, or a boat rider, held by the same climb or boat last frame and free now, is frozen again; the
	 * ladder's own end (its camera model gone) and Disembark (m_pBoat gone) are not undone. */
	{
		uint32_t model = 0x5678, none = 0, rider = (uint32_t)(uintptr_t)private_data[2];
		edict_t *boat;
		setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = guest; guest->v.flags = FL_FROZEN;
		memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &model, 4 ); SV_CoF_MomentsFrame();
		guest->v.flags = 0; guest->v.velocity[0] = 50; SV_CoF_MomentsFrame();
		assert(( guest->v.flags & FL_FROZEN ) && !guest->v.velocity[0] && logged( "held-refrozen userid=101 by=ladder" ) == 1 );
		guest->v.flags = 0; SV_CoF_MomentsFrame(); assert(( guest->v.flags & FL_FROZEN ) && logged( "held-refrozen" ) == 1 ); /* told once */
		guest->v.flags = 0; memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &none, 4 ); SV_CoF_MomentsFrame(); /* the ladder's end */
		assert( !( guest->v.flags & FL_FROZEN ));
		/* another camera model (a model cutscene after the climb), or never held: left free */
		setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = guest; guest->v.flags = FL_FROZEN;
		memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &model, 4 ); SV_CoF_MomentsFrame();
		model = 0x9999; memcpy( private_data[2] + COF_PL_CAMERA_MODEL, &model, 4 ); guest->v.flags = 0; SV_CoF_MomentsFrame();
		assert( !( guest->v.flags & FL_FROZEN ) && !logged( "held-refrozen" ));
		SV_CoF_MomentsFrame(); assert( !( guest->v.flags & FL_FROZEN ));
		/* the boat's rider, on board */
		setup(); boat = spawn( 16, 28, 0, 0 ); memcpy( private_data[16] + COF_BOAT_RIDER, &rider, 4 ); set_handle( 2, COF_PL_BOAT, boat );
		guest->v.flags = FL_FROZEN; SV_CoF_MomentsFrame(); guest->v.flags = 0; SV_CoF_MomentsFrame();
		assert(( guest->v.flags & FL_FROZEN ) && logged( "held-refrozen userid=101 by=boat" ) == 1 );
		/* Disembark: the boat lets go and m_pBoat is cleared with the control */
		memcpy( private_data[16] + COF_BOAT_RIDER, &none, 4 ); set_handle( 2, COF_PL_BOAT, NULL ); guest->v.flags = 0;
		SV_CoF_MomentsFrame(); assert( !( guest->v.flags & FL_FROZEN ));
		/* boarding (no m_pBoat yet), or a dead rider: left as it is */
		setup(); boat = spawn( 16, 28, 0, 0 ); memcpy( private_data[16] + COF_BOAT_RIDER, &rider, 4 );
		guest->v.flags = FL_FROZEN; SV_CoF_MomentsFrame(); guest->v.flags = 0; SV_CoF_MomentsFrame(); assert( !( guest->v.flags & FL_FROZEN ));
		set_handle( 2, COF_PL_BOAT, boat ); guest->v.flags = FL_FROZEN; SV_CoF_MomentsFrame(); guest->v.flags = 0; guest->v.health = 0;
		SV_CoF_MomentsFrame(); assert( !( guest->v.flags & FL_FROZEN ));
	}
	/* 1.1.9 (S5): a merged map: the host in section 0, the guest in section 1. A scene of section 0 (a manager's think
	 * there) shows its fade, black-and-white, chapter title and subtitles to the host only; what could leave a screen
	 * dark or grey always goes, as do "Game over", the engine's own lines and anything of the guest's own section. */
	{
		byte fade_out[10] = { 0, 8, 0, 0, 1 | 4, 0, 0, 0, 0, 255 }, fade_in[10] = { 0, 8, 0, 0, 0, 0, 0, 0, 0, 255 };
		byte bw_on = 255, bw_off = 0, chapter[] = "Chapter 3\0", over[] = "Game over\0", subtitle[] = "\5Hello?", own[64];
		int own_size = CoF_StripPayload( own, sizeof( own ), 5, "Little Rascal 1 is taking the call." );
		edict_t *mm, *brush, *delayed;
		setup(); merged = 1; set_vec( host->v.origin, 0, 0, 100 ); set_vec( guest->v.origin, 0, 0, 1100 );
		mm = spawn( 11, 16, 21, 0 ); set_vec( mm->v.origin, 50, 50, 200 ); running_think = mm;
		/* the guest's screen is not known at first: a fade goes; then it is clear, and the next scene fade is held */
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_in, 10, NULL ));
		assert( !SV_CoF_MomentsHeldBack( host, MSG_ONE, "ScreenFade", fade_out, 10, NULL ));
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_out, 10, NULL ));
		assert( logged( "effect-held name=ScreenFade userid=101 section=1 from=0" ) == 1 );
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_in, 10, NULL ));       /* consistent */
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "BlackWhite", &bw_on, 1, NULL ) && !SV_CoF_MomentsHeldBack( host, MSG_ONE, "BlackWhite", &bw_on, 1, NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "BlackWhite", &bw_off, 1, NULL ));        /* off always goes */
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", over, (int)sizeof( over ), NULL ));
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE_UNRELIABLE, "ProFont", subtitle, (int)sizeof( subtitle ), NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ProFont", own, own_size, NULL ));        /* the engine's own line */
		assert( logged( "effect-held" ) == 1 );                                                 /* told at most every 2 s */
		/* a screen a fade left dark gets every fade: an unknown source (no think, no player) darkened it */
		running_think = NULL; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_out, 10, NULL ));
		running_think = mm; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_in, 10, NULL ));
		/* grey left on by an unknown source: a scene's grey of another section goes too */
		running_think = NULL; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "BlackWhite", &bw_on, 1, NULL ));
		running_think = mm; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "BlackWhite", &bw_on, 1, NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "BlackWhite", &bw_off, 1, NULL ));
		/* where it comes from: a brush entity by its bounds (its origin is 0 0 0), a delayed trigger by its activator,
		 * a player's command or move by them; the receiver's own section: as retail */
		brush = spawn( 12, 20, 0, 0 ); brush->v.model = 36; set_vec( brush->v.absmin, 0, 0, 1050 ); set_vec( brush->v.absmax, 10, 10, 1250 );
		running_think = brush; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		assert( SV_CoF_MomentsHeldBack( host, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		delayed = spawn( 13, 32, 0, 0 ); set_handle( 13, COF_DELAY_ACTIVATOR, host ); running_think = delayed;
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		set_handle( 13, COF_DELAY_ACTIVATOR, NULL ); assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		running_think = NULL; assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), host ));
		/* another entity's think (a phone call's lines are its callee's): not known, as retail */
		running_think = spawn( 14, 5, 6, 7 ); set_vec( running_think->v.origin, 0, 0, 200 );
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ProFont", subtitle, (int)sizeof( subtitle ), NULL )); running_think = NULL;
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), guest ));
		/* to everyone, another message, a companion, a map not merged, the story layer off: as retail */
		running_think = mm;
		assert( !SV_CoF_MomentsHeldBack( NULL, MSG_ALL, "ProFont", subtitle, (int)sizeof( subtitle ), NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "PlayMP3", subtitle, (int)sizeof( subtitle ), NULL ));
		clients[1].flags = FCL_FAKECLIENT; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		clients[1].flags = 0; merged = 0; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL ));
		merged = 1; gate = 0; assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "Chapter", chapter, (int)sizeof( chapter ), NULL )); gate = 1;
		/* a stay-out fade to everyone: every screen may be dark, so the next fades go */
		assert( !SV_CoF_MomentsHeldBack( NULL, MSG_ALL, "ScreenFade", fade_out, 10, NULL ));
		assert( !SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_in, 10, NULL ));
		assert( SV_CoF_MomentsHeldBack( guest, MSG_ONE, "ScreenFade", fade_in, 10, NULL ));
		running_think = NULL;
	}
	puts( "moments scenarios passed" );
	return 0;
}
