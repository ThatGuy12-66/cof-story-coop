/* The 12-slot inventory's server rules (sv_cof_inventory12.c) against engine
 * doubles and a fake hl.dll image: the per-map slot count, the keep and wide
 * groups, the cof_inv_slots stufftext, join refusal and the command filter. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define ss_loading 1
#define ss_active 2
#define FCL_FAKECLIENT (1<<7)
#define svc_stufftext 9
#define FBitSet(f,b) ((f)&(b))
#define Q_strcmp strcmp
#ifdef _WIN32
#define Q_stricmp _stricmp
#else
#include <strings.h>
#define Q_stricmp strcasecmp
#endif
#define Q_atoi atoi
#define Host_IsDedicated() ( dedicated )
#define COF_INV12_USERINFO "_cofinv" /* sv_cof_inventory12.h */
typedef struct { float value; const char *string; } convar_t;
typedef struct { char text[512]; } sizebuf_t;
typedef struct { sizebuf_t message; } netchan_t;
typedef struct { byte *pv; } edict_t; /* only its private data */
typedef struct { int state, userid; unsigned int flags; char userinfo[256]; netchan_t netchan; edict_t *edict; } sv_client_t;
static struct { int state; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { void *hInstance; } svgame;
static sv_client_t clients[MAX_CLIENTS];
static edict_t edicts[MAX_CLIENTS];
static byte player_pv[MAX_CLIENTS][0x2288]; /* CBasePlayer up to +0x2288 */
static convar_t slots_cvar = { 12, "12" };
static int story = 1, dedicated, host_ready = 1, three, wrote;
static unsigned char image[0x100000];
static char log_text[8192];
static size_t log_length;
static void (*status_f)( void );
static const char *args[4];
static int argc;

static void Q_strncpy( char *d, const char *s, size_t n ) { size_t i; for( i = 0; i + 1 < n && s[i]; i++ ) d[i] = s[i]; d[i] = 0; }
static void Con_Printf( const char *fmt, ... )
{
	va_list a; int n;
	va_start( a, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, a );
	va_end( a );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static char *va( const char *fmt, ... )
{
	static char buf[128]; va_list a;
	va_start( a, fmt ); vsnprintf( buf, sizeof( buf ), fmt, a ); va_end( a );
	return buf;
}
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *description )
{
	assert( !strcmp( name, "cof_inventory_slots" ) && !strcmp( value, "12" ));
	return &slots_cvar;
}
static void Cmd_AddCommand( const char *name, void (*f)( void ), const char *description )
{
	assert( !strcmp( name, "cof_inventory12_status" ));
	status_f = f;
}
static const char *Cmd_Argv( int i ) { return i < argc ? args[i] : ""; }
static const char *Info_ValueForKey( const char *s, const char *key )
{
	static char value[64];
	char k[64];
	while( *s == '\\' )
	{
		int n = 0;
		for( s++; *s && *s != '\\' && n < 63; ) k[n++] = *s++;
		k[n] = 0;
		if( *s != '\\' ) break;
		for( s++, n = 0; *s && *s != '\\' && n < 63; ) value[n++] = *s++;
		value[n] = 0;
		if( !strcmp( k, key )) return value;
	}
	value[0] = 0;
	return value;
}
static qboolean SV_CoF_StoryCoopEnabled( void ) { return story; }
static uintptr_t SV_CoF_StoryBase( void ) { return story ? (uintptr_t)image : 0; }
static qboolean CL_CoF_Inventory12Ready( void ) { return host_ready; }
static qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl ) { return three; }
static char told_text[MAX_CLIENTS][192]; /* the last ProFont line to each client */
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	assert( colour == 5 && reliable );
	snprintf( told_text[cl - clients], sizeof( told_text[0] ), "%s", text );
	return true;
}
static byte *SV_CoF_StoryEntityPrivate( edict_t *ent, size_t need )
{
	assert( need >= 0x2288 ); /* every byte read is inside the checked allocation */
	return ent && need <= sizeof( player_pv[0] ) ? ent->pv : NULL;
}
static void MSG_BeginServerCmd( sizebuf_t *msg, int cmd ) { assert( cmd == svc_stufftext ); }
static void MSG_WriteString( sizebuf_t *msg, const char *s ) { strcat( msg->text, s ); }
static int Fake( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
	if( memcmp( p, expect, len )) return 0;
	memcpy( p, value, len );
	wrote++;
	return 1;
}
#define COF_INV12_WRITE Fake

#include "patch_under_test.h"
#include "inventory12_under_test.c"

#define KEEP_STATE CoF_Inv12GroupState( image, cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep ))
#define WIDE_STATE CoF_Inv12GroupState( image, cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide ))

static int logged( const char *text )
{
	int n = 0; const char *p = log_text;
	while(( p = strstr( p, text ))) { n++; p += strlen( text ); }
	return n;
}
static void stock_image( void )
{
	int i;
	memset( image, 0xcc, sizeof( image ));
	for( i = 0; i < COF_INV12_COUNT( cof_inv12_keep ); i++ ) memcpy( image + cof_inv12_keep[i].rva, cof_inv12_keep[i].stock, cof_inv12_keep[i].len );
	for( i = 0; i < COF_INV12_COUNT( cof_inv12_wide ); i++ ) memcpy( image + cof_inv12_wide[i].rva, cof_inv12_wide[i].stock, cof_inv12_wide[i].len );
}
static void person( int i, int userid, const char *userinfo )
{
	clients[i].state = cs_spawned; clients[i].userid = userid; clients[i].flags = 0;
	strcpy( clients[i].userinfo, userinfo ); clients[i].netchan.message.text[0] = 0;
}
static void map( void ) { svs.spawncount++; sv.state = ss_loading; SV_CoF_Inventory12Activate(); sv.state = ss_active; }
static qboolean command( const char *a0, const char *a1, const char *a2 )
{
	args[0] = a0; args[1] = a1; args[2] = a2; argc = a2 ? 3 : a1 ? 2 : 1;
	return SV_CoF_Inventory12ClientCommand( &clients[0] );
}

int main( void )
{
	int i;
	svs.maxclients = 2; svs.clients = clients; svgame.hInstance = (void *)1;
	for( i = 0; i < MAX_CLIENTS; i++ ) { edicts[i].pv = player_pv[i]; clients[i].edict = &edicts[i]; }
	stock_image();
	SV_CoF_Inventory12Init();
	assert( SV_CoF_InventorySlots() == 6 ); /* nothing before the first map */

	/* a listen host whose own client has the grid, nobody connected yet: 12 */
	map();
	assert( SV_CoF_InventorySlots() == 12 && KEEP_STATE == 1 && WIDE_STATE == 1 );
	assert( logged( "cofstory phase=inventory12 slots=12\n" ) == 1 );

	/* every person is told once per map; the AI companion never */
	person( 0, 1, "\\name\\host\\_cofinv\\12" );
	SV_CoF_Inventory12Frame();
	SV_CoF_Inventory12Frame();
	assert( !strcmp( clients[0].netchan.message.text, "cof_inv_slots 12\n" ));
	clients[1].state = cs_connected; clients[1].flags = FCL_FAKECLIENT;
	SV_CoF_Inventory12Frame();
	assert( !clients[1].netchan.message.text[0] );
	clients[1].state = cs_free; clients[1].flags = 0;

	/* told only once spawned: the listen host's own client clears its count at
	 * svc_serverdata, after a stufftext sent while it was connecting */
	person( 1, 7, "\\name\\guest\\_cofinv\\12" ); clients[1].state = cs_connected;
	SV_CoF_Inventory12Frame();
	assert( !clients[1].netchan.message.text[0] );
	clients[1].state = cs_spawned;
	SV_CoF_Inventory12Frame();
	SV_CoF_Inventory12Frame();
	assert( !strcmp( clients[1].netchan.message.text, "cof_inv_slots 12\n" ));
	/* a reconnect into the same slot on the same map: told again */
	clients[1].netchan.message.text[0] = 0;
	clients[1].state = cs_connected; SV_CoF_Inventory12Frame();
	assert( !clients[1].netchan.message.text[0] );
	clients[1].state = cs_spawned; SV_CoF_Inventory12Frame();
	assert( !strcmp( clients[1].netchan.message.text, "cof_inv_slots 12\n" ));
	/* someone else in the slot, never seen below spawned: told */
	clients[1].netchan.message.text[0] = 0;
	clients[1].userid = 8; SV_CoF_Inventory12Frame();
	assert( !strcmp( clients[1].netchan.message.text, "cof_inv_slots 12\n" ));
	clients[1].state = cs_free;
	assert( !strcmp( clients[0].netchan.message.text, "cof_inv_slots 12\n" )); /* the host still once */

	/* joining a map at 12 needs the 12-slot screen; the host's own client is never refused */
	assert( SV_CoF_Inventory12Refuse( "\\name\\old", false ));
	assert( logged( "cofstory phase=inventory12-join-refused name=old\n" ) == 1 );
	assert( !SV_CoF_Inventory12Refuse( "\\name\\new\\_cofinv\\12", false ));
	assert( !SV_CoF_Inventory12Refuse( "\\name\\old", true ));

	/* the command filter at 12 */
	assert( !command( "inventoryequip", "12", NULL ));
	assert( command( "inventoryequip", "13", NULL ));
	assert( command( "inventorydrop", "0", NULL ));
	assert( command( "inventoryuse", "7x", NULL ));
	assert( command( "inventoryequip", "0012", NULL )); /* longer than two digits: refused, */
	assert( command( "inventoryequip", "012", NULL ));  /* whatever atoi makes of it */
	assert( command( "inventoryequip", NULL, NULL ));
	assert( !command( "inventorycombine", "6", "7" ));
	assert( command( "inventorycombine", "6", "13" ));
	assert( command( "inventorydualwield", "13", "2" ));
	assert( !command( "quickselset2", "9", NULL ));
	assert( !command( "inventorydeletenamed", "inventoryitems/x.txt", "0" )); /* not a slot number */
	assert( logged( "cofstory phase=inventory-cmd-refused userid=1 cmd=inventoryequip args=\"13 \"\n" ) == 1 );
	three = 1; /* the story's three-slot sections */
	assert( command( "inventoryequip", "4", NULL ));
	assert( !command( "inventoryequip", "3", NULL ));
	three = 0;

	/* a person without the screen connected at map load: six, the keep group stays */
	person( 1, 2, "\\name\\old" );
	map();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 1 && WIDE_STATE == 0 );
	assert( logged( "cofstory phase=inventory12-off reason=client:2\n" ) == 1 );
	SV_CoF_Inventory12Frame();
	assert( strstr( clients[0].netchan.message.text, "cof_inv_slots 6\n" ));
	assert( command( "inventoryequip", "7", NULL ));
	assert( !SV_CoF_Inventory12Refuse( "\\name\\old", false )); /* a map at six takes anyone */
	/* 1.1.9 (I10, M3): a person who carries something in slots 7..12 is told those are kept, once a map; one with nothing
	 * there, or in three-slot mode, is not */
	assert( strstr( told_text[0], "slots 7 to 12 is kept" ) == NULL && !logged( "inventory12-hidden" )); /* the host carried nothing there */
	{
		uint32_t id = 0x1234;
		memcpy( player_pv[0] + 0x21FC + 9 * 4, &id, sizeof( id )); /* inventorySlot[9] */
		clients[0].netchan.message.text[0] = 0; told_text[0][0] = 0;
		map(); SV_CoF_Inventory12Frame(); SV_CoF_Inventory12Frame();
		assert( strstr( told_text[0], "slots 7 to 12 is kept" ) && logged( "cofstory phase=inventory12-hidden userid=1\n" ) == 1 );
		assert( !told_text[1][0] ); /* the old client carried nothing there */
		three = 1; map(); SV_CoF_Inventory12Frame(); three = 0;
		assert( logged( "inventory12-hidden" ) == 1 );
		id = 0; memcpy( player_pv[0] + 0x21FC + 9 * 4, &id, sizeof( id ));
	}

	/* six after twelve: 12 again on the next map once everyone has the screen */
	clients[1].state = cs_free;
	map();
	assert( SV_CoF_InventorySlots() == 12 && WIDE_STATE == 1 );

	/* the cvar, a host client without the grid, a dedicated server */
	slots_cvar.value = 6; map();
	assert( SV_CoF_InventorySlots() == 6 && logged( "reason=cvar\n" ) == 1 );
	slots_cvar.value = 12; host_ready = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && logged( "reason=host-client\n" ) == 1 );
	dedicated = 1; map();
	assert( SV_CoF_InventorySlots() == 12 );
	dedicated = 0; host_ready = 1;

	/* per spawned person, the fields after the slot array (bools as one byte, floats, raw
	 * dwords) and the nonzero dwords in +0x2230..+0x226B, which nothing may write */
	{
		byte *pv = player_pv[0];
		float f;
		unsigned int d;
		pv[0x226C] = 1; pv[0x226D] = 0x7f; /* m_bUsedFirstSyringe */
		f = 1.5f; memcpy( pv + 0x2270, &f, sizeof( f ));
		f = 2.25f; memcpy( pv + 0x2274, &f, sizeof( f )); /* m_fMorphineTripTime */
		pv[0x2278] = 1; pv[0x2279] = 0x7f; /* m_bIsTripping */
		f = 0.5f; memcpy( pv + 0x227C, &f, sizeof( f )); /* m_fTrippingTime */
		d = 0x11223344; memcpy( pv + 0x2280, &d, sizeof( d ));
		d = 0xdeadbeef; memcpy( pv + 0x2284, &d, sizeof( d ));
		pv[0x2230] = 1; pv[0x226B] = 0x80; /* two dead dwords */
		pv[0x222F] = 1; /* slot 12's last byte: not dead */
	}
	person( 1, 5, "\\name\\guest\\_cofinv\\12" ); clients[1].state = cs_connected; /* not spawned: no line */
	status_f();
	assert( logged( "cofstory phase=inventory12-status slots=12 cvar=12 keep=1 wide=1 reason=none\n" ) == 1 );
	assert( logged( "cofstory phase=inventory12-fields userid=1 first_syringe=1 f2270=1.5 trip_time=2.25 tripping=1 tripping_time=0.5 f2280=11223344 f2284=deadbeef dead=2\n" ) == 1 );
	assert( logged( "phase=inventory12-fields" ) == 1 );
	clients[1].state = cs_free;

	/* story co-op off mid-map: everyone still connected leaves the grid, then the
	 * gate's close puts every byte back; nothing is filtered */
	clients[0].netchan.message.text[0] = 0;
	person( 1, 6, "\\name\\guest\\_cofinv\\12" ); clients[1].state = cs_connected;
	SV_CoF_Inventory12Close();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 0 && WIDE_STATE == 0 );
	assert( !strcmp( clients[0].netchan.message.text, "cof_inv_slots 6\n" ) && !strcmp( clients[1].netchan.message.text, "cof_inv_slots 6\n" ));
	SV_CoF_Inventory12Close(); /* at six already: said once */
	assert( !strcmp( clients[0].netchan.message.text, "cof_inv_slots 6\n" ) && !strcmp( clients[1].netchan.message.text, "cof_inv_slots 6\n" ));
	clients[1].state = cs_free;
	/* not while the server is not active, nor without clients */
	map(); assert( SV_CoF_InventorySlots() == 12 );
	clients[0].netchan.message.text[0] = 0;
	sv.state = ss_loading; SV_CoF_Inventory12Close(); sv.state = ss_active;
	assert( SV_CoF_InventorySlots() == 6 && !clients[0].netchan.message.text[0] );
	map(); assert( SV_CoF_InventorySlots() == 12 );
	svs.clients = NULL; SV_CoF_Inventory12Close(); svs.clients = clients;
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 0 && WIDE_STATE == 0 && !clients[0].netchan.message.text[0] );
	story = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && KEEP_STATE == 0 );
	assert( !command( "inventoryequip", "13", NULL ));
	person( 0, 9, "\\name\\host\\_cofinv\\12" ); /* a new map and a new person: still nothing said */
	SV_CoF_Inventory12Frame();
	assert( !clients[0].netchan.message.text[0] );
	story = 1;

	/* unknown bytes: six, nothing written */
	image[cof_inv12_keep[3].rva] = 0x90;
	wrote = 0; map();
	assert( SV_CoF_InventorySlots() == 6 && wrote == 0 && logged( "reason=bytes\n" ) == 1 );
	puts( "inventory12 server rules passed" );
	return 0;
}
