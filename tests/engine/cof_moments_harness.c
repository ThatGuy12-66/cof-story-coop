/* Story moments (sv_cof_moments.c) against engine doubles: no game DLL,
 * networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
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
#define SOLID_SLIDEBOX 3
#define FL_GODMODE (1<<6)
#define FL_FROZEN (1<<12)
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define IN_USE (1<<5)
#define COF_RETAIL_SLOTS 12
#define COF_RETAIL_ITEM 64
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
#define Q_atoi atoi
#define Q_snprintf snprintf
#define SetBits(f,b) ((f)|=(b))
#define FBitSet(f,b) ((f)&(b))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
typedef struct edict_s edict_t;
typedef struct {
	string_t classname, target, targetname;
	vec3_t origin, absmin, absmax;
	float health, nextthink, fuser3;
	int deadflag, flags, spawnflags, iuser1, solid;
	edict_t *owner;
} entvars_t;
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
	"ryesno", "yesno", "multi_manager", "lad2" };
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
static int logos_to[MAX_CLIENTS], logo_value;
static void SV_CoF_MomentsMessage( edict_t *to, const char *name, const byte *payload, int size );
/* as the engine: the message goes out, and is seen by the moments hook like any other */
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	assert( !strcmp( name, "CoFLogo" ) && size == 1 && reliable );
	logos_to[NUM_FOR_EDICT( to ) - 1]++; logo_value = payload[0];
	SV_CoF_MomentsMessage( to, name, payload, size );
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
static void yesno_message( int slot, int index )
{
	byte payload[32]; int n = (int)strlen( "gfx/yesno.tga" ) + 1;
	memcpy( payload, "gfx/yesno.tga", n ); payload[n] = (byte)( index & 0xFF ); payload[n + 1] = (byte)( index >> 8 );
	SV_CoF_MomentsMessage( clients[slot].edict, "YesNo", payload, n + 2 );
}

int main( void )
{
	edict_t *host = &entities[1], *guest = &entities[2], *call, *chair, *manager, *use;
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

	/* Ladders: while the host climbs, nobody near its use brushes can start it. */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = host; host->v.flags = FL_FROZEN | FL_GODMODE;
	use = spawn( 16, 9, 0, 10 ); use->v.absmin[0] = 480; use->v.absmax[0] = 520; use->v.absmax[1] = use->v.absmax[2] = 10;
	SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE | 1; SV_CoF_MomentsCommand( &clients[1], &cmd );
	assert( cmd.buttons == 1 && !strcmp( prompts[1], "5:Little Rascal 1 is climbing. Wait a moment." ) && logged( "ladder-busy userid=101 climber=100" ) == 1 );
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == 0 && logged( "ladder-busy" ) == 1 ); /* told once */
	/* far away, or the climb over: +use goes through */
	guest->v.origin[0] = 900; cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	guest->v.origin[0] = 500; host->v.flags = 0; SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );
	/* another ladder's brush is not this one's */
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = host; host->v.flags = FL_FROZEN | FL_GODMODE;
	use = spawn( 16, 9, 0, 17 ); use->v.absmin[0] = 480; use->v.absmax[0] = 520; SV_CoF_MomentsFrame();
	cmd.buttons = IN_USE; SV_CoF_MomentsCommand( &clients[1], &cmd ); assert( cmd.buttons == IN_USE );

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
	setup(); manager = spawn( 15, 8, 10, 0 ); manager->v.owner = guest; guest->v.flags = FL_FROZEN | FL_GODMODE;
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
	puts( "moments scenarios passed" );
	return 0;
}
