/* Offline doubles for sv_cof_retail.c: no game DLL, network or game process. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef long long fs_offset_t;
#define true 1
#define false 0
#define cs_spawned 4
#define MAX_CLIENTS 32
#define FL_KILLME ( 1 << 30 )
#define FL_ONGROUND ( 1 << 9 )
#define DEAD_NO 0
#define DEAD_DEAD 2
#define ARRAYSIZE( x ) ( sizeof( x ) / sizeof(( x )[0] ))
#define NUM_FOR_EDICT( e ) ((int)(( e ) - edicts ))
#define FBitSet( x, b ) ((( x ) & ( b )) != 0 )
#define Q_atof( s ) ((float)atof( s ))
#define S_USAGE "Usage: "
#define bound( lo, x, hi ) (( x ) < ( lo ) ? ( lo ) : (( x ) > ( hi ) ? ( hi ) : ( x )))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_strlen strlen
#define Q_strstr strstr
#define Q_atoi atoi
#define Q_snprintf snprintf
#define Q_max( a, b ) (( a ) > ( b ) ? ( a ) : ( b ))
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
typedef struct { float health, fuser3; int deadflag, flags, spawnflags; string_t classname; } entvars_t;
typedef struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef void ( *xcommand_t )( void );
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; } sv;
static struct { struct { void ( *pfnClientCommand )( edict_t *ent ); int ( *pfnSpawn )( edict_t *ent ); void ( *pfnUse )( edict_t *ent, edict_t *player ); } dllFuncs; int numEntities; edict_t *edicts; } svgame;
static edict_t edicts[12];
static sv_client_t clients[2];
static byte private_data[2][0x2820], doc_private[0x210], weapon_private[4][0x210], monster_private[3][0x360];
static const char *strings[] = { "", "inventoryitems/hallkey.txt", "inventoryitems/weapons/weapon_mobile.txt", "inventoryitems/fuse.txt", "cof_document",
	"weapon_glock", "weapon_switchblade", "weapon_syringe", "weapon_shotgun", "monster_slower", "monster_generic", "monster_sawrunner", "monster_nerd",
	"weapon_dualweapon", "findthekey", "txtfiles/custom_objectives.txt", "txtfiles/hints.txt" };
#define STRINGS ( (int)ARRAYSIZE( strings ))
static byte dll_image[0x217118 + 19 * 19 * 4]; /* the relationship table sits at +0x217118 */
static int private_lookups, uses;
static edict_t *used_body, *used_by;
static unsigned int call_rva;
static void *call_self, *call_arg;
static int calls;
static int gate = 1, cheats = 0, give_calls, give_result = 1, argc;
static const char *argv[8];
static char last_command[256], last_give_class[64], last_give_message[128], last_msg_name[32], console[8192];
static edict_t *last_command_edict;
static char fs_paths[1024]; /* every path the code asked the filesystem for */
static byte last_msg[256];
static int last_msg_size, last_msg_reliable;
static size_t console_length;
static xcommand_t probe;

static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static size_t private_size( const void *pv )
{
	int i;
	if( pv == doc_private ) return sizeof( doc_private );
	for( i = 0; i < 4; i++ ) if( pv == weapon_private[i] ) return sizeof( weapon_private[0] );
	for( i = 0; i < 3; i++ ) if( pv == monster_private[i] ) return sizeof( monster_private[0] );
	return sizeof( private_data[0] );
}
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need )
{
	private_lookups++;
	return e && e->pvPrivateData && need <= private_size( e->pvPrivateData ) ? (byte *)e->pvPrivateData : NULL;
}
static uintptr_t SV_CoF_StoryBase( void ) { return gate ? (uintptr_t)dll_image : 0; }
static const char *SV_GetString( string_t s ) { assert( s >= 0 && s < STRINGS ); return strings[s]; }
static string_t SV_AllocString( const char *text )
{
	int index;
	for( index = 0; index < STRINGS; index++ ) if( !strcmp( text, strings[index] )) return index;
	assert( 0 );
	return 0;
}
static edict_t *SV_CreateNamedEntity( edict_t *entity, string_t classname ) { return NULL; }
static void SV_FreeEdict( edict_t *entity ) { entity->free = 1; }
static edict_t *SV_EdictNum( int i ) { return &edicts[i]; }
static qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
	calls++; call_rva = rva; call_self = self; call_arg = arg;
	return true;
}
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static int Cvar_VariableInteger( const char *name ) { return !strcmp( name, "sv_cheats" ) ? cheats : 0; }
static byte *FS_LoadFile( const char *path, fs_offset_t *size, qboolean gamedironly )
{
	static const char hallkey[] = "\"map_name\"\r\n{\r\n\tdata\t\"hallkey\"\r\n}\r\n\"nice_name\"\r\n{\r\n\tdata\t\"Hall key\"\r\n}\r\n\"classname\"\r\n{\r\n\tdata\t\"item_key\"\r\n}\r\n";
	static const char mobile[] = "\"classname\"\r\n{\r\n\tdata\t\"weapon_mobile\"\r\n}\r\n";
	const char *text;
	strncat( fs_paths, path, sizeof( fs_paths ) - strlen( fs_paths ) - 2 );
	strcat( fs_paths, "|" );
	text = !strcmp( path, "inventoryitems/hallkey.txt" ) ? hallkey : !strcmp( path, "inventoryitems/weapons/weapon_mobile.txt" ) ? mobile : NULL;
	char *copy;
	if( !text ) return NULL;
	copy = malloc( strlen( text ) + 1 );
	strcpy( copy, text );
	if( size ) *size = (fs_offset_t)strlen( text );
	return (byte *)copy;
}
static void Mem_Free( void *p ) { free( p ); }
static qboolean SV_CoF_StoryGiveItemEx( sv_client_t *cl, const char *classname, const char *message )
{
	give_calls++;
	Q_strncpy( last_give_class, classname, sizeof( last_give_class ));
	Q_strncpy( last_give_message, message, sizeof( last_give_message ));
	return give_result;
}
static void Cmd_TokenizeString( const char *text ) { Q_strncpy( last_command, text, sizeof( last_command )); }
/* ClientCommand drops every command of a player in a phone call (m_bInPhoneCall +0x1EE8) or on the computer or
 * phone screen (m_iUsingComputerOrPhone +0x1EA0): seen here as the gates at the time of the call */
static int command_saw_call = -1, command_saw_screen = -1;
static void ClientCommand( edict_t *e )
{
	last_command_edict = e;
	if( e && e->pvPrivateData ) { command_saw_call = ((byte *)e->pvPrivateData)[0x1EE8]; memcpy( &command_saw_screen, (byte *)e->pvPrivateData + 0x1EA0, 4 ); }
}
/* The player's Use: the game's CPR start, here only its links (reviver -> body, body -> reviver). */
static void game_use( edict_t *body, edict_t *by )
{
	byte *b = body->pvPrivateData, *r = by->pvPrivateData;
	int serial;
	uses++; used_body = body; used_by = by;
	if( !r || !b ) return;
	memcpy( r + 0x2698, &body, sizeof( body )); serial = body->serialnumber; memcpy( r + 0x2698 + sizeof( body ), &serial, sizeof( serial ));
	memcpy( b + 0x26A0, &by, sizeof( by )); serial = by->serialnumber; memcpy( b + 0x26A0 + sizeof( by ), &serial, sizeof( serial ));
}
static void handle( byte *field, edict_t *e ) { int serial = e ? e->serialnumber : 0; memcpy( field, &e, sizeof( e )); memcpy( field + sizeof( e ), &serial, sizeof( serial )); }
static void put_int( byte *field, int value ) { memcpy( field, &value, sizeof( value )); }
static void put_float( byte *field, float value ) { memcpy( field, &value, sizeof( value )); }
static void put_pointer( byte *field, const void *value ) { memcpy( field, &value, sizeof( value )); }
/* A carried weapon: entity i, its object, its owner, m_iId, clip and ammo type. */
static void weapon( int i, int w, string_t classname, byte *owner, int id, int clip, int type )
{
	edicts[i].free = 0; edicts[i].v.classname = classname; edicts[i].pvPrivateData = weapon_private[w];
	memset( weapon_private[w], 0, sizeof( weapon_private[w] ));
	put_pointer( weapon_private[w] + 0x160, owner ); put_int( weapon_private[w] + 0x168, id );
	put_int( weapon_private[w] + 0x1E4, clip ); put_int( weapon_private[w] + 0x1DC, type );
}
static int ammo( int player, int type ) { int value; memcpy( &value, private_data[player] + 0x2540 + type * 4, sizeof( value )); return value; }
static void monster( int i, int m, string_t classname, int react, int keyed )
{
	edicts[i].free = 0; edicts[i].v.classname = classname; edicts[i].v.spawnflags = 0; edicts[i].pvPrivateData = monster_private[m];
	memset( monster_private[m], 0, sizeof( monster_private[m] ));
	put_int( monster_private[m] + 0x354, react ); put_int( monster_private[m] + 0x350, keyed );
}
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	Q_strncpy( last_msg_name, name, sizeof( last_msg_name ));
	memcpy( last_msg, payload, size );
	last_msg_size = size;
	last_msg_reliable = reliable;
	return true;
}
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int i ) { return i < argc ? argv[i] : ""; }
static void Cmd_AddCommand( const char *name, xcommand_t f, const char *desc ) { probe = f; }

#include "sv_cof_descriptor.h"
#include "sv_cof_retail.h"
#include "sv_cof_light.h"
static int inventory_slots = 6;
static int SV_CoF_InventorySlots( void ) { return inventory_slots; }
#include "retail_under_test.c"

int main( void )
{
	char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM], nice[64];
	string_t id;
	int i;
	static const byte fade[10] = { 0x00, 0x08, 0x99, 0x01, 0x00, 0x00, 0, 0, 0, 255 };

	svs.maxclients = 2;
	svs.clients = clients;
	svgame.dllFuncs.pfnClientCommand = ClientCommand;
	svgame.edicts = edicts;
	svgame.numEntities = 6;
	svgame.dllFuncs.pfnUse = game_use;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i + 1];
		edicts[i + 1].pvPrivateData = private_data[i];
	}
	SV_CoF_RetailInit();
	assert( probe );

	/* slots: usable slots 1..6 only; the game never uses slot 0 */
	id = 2; memcpy( private_data[0] + 0x21FC, &id, sizeof( id ));
	id = 1; memcpy( private_data[0] + 0x21FC + 1 * sizeof( id ), &id, sizeof( id ));
	id = 3; memcpy( private_data[0] + 0x21FC + 6 * sizeof( id ), &id, sizeof( id ));
	assert( SV_CoF_RetailReadSlots( &clients[0], slots ));
	assert( !strcmp( slots[0], "inventoryitems/hallkey.txt" ) && !slots[1][0] && !strcmp( slots[5], "inventoryitems/fuse.txt" ));
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 4 );
	private_data[0][0x1E9C] = 1; /* three-slot mode: only slots 1..3 count */
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 2 );
	private_data[0][0x1E9C] = 0;
	/* twelve slots: the engine reads all of them; free slots count to the map's size */
	inventory_slots = 12;
	id = 3; memcpy( private_data[0] + 0x21FC + 12 * sizeof( id ), &id, sizeof( id ));
	assert( SV_CoF_RetailReadSlots( &clients[0], slots ) && !strcmp( slots[11], "inventoryitems/fuse.txt" ) && !slots[6][0] );
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 9 );
	private_data[0][0x1E9C] = 1;
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 2 && SV_CoF_RetailInThreeSlots( &clients[0] ));
	private_data[0][0x1E9C] = 0;
	inventory_slots = 6; /* a map at six: slot 12 is kept but not counted */
	assert( SV_CoF_RetailFreeSlots( &clients[0] ) == 4 && !SV_CoF_RetailInThreeSlots( &clients[0] ));
	id = 0; memcpy( private_data[0] + 0x21FC + 12 * sizeof( id ), &id, sizeof( id ));
	clients[1].state = 2; /* not spawned: nothing is read */
	assert( !SV_CoF_RetailReadSlots( &clients[1], slots ) && SV_CoF_RetailFreeSlots( &clients[1] ) == -1 );
	clients[1].state = cs_spawned;

	/* give: the classname comes from the descriptor, through the game's pickup */
	assert( SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/hallkey.txt" ));
	assert( !strcmp( last_give_class, "item_key" ) && !strcmp( last_give_message, "inventoryitems/hallkey.txt" ));
	give_calls = 0;
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/weapons/weapon_mobile.txt" )); /* not a story item class */
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/fuse.txt" )); /* no descriptor file */
	assert( !SV_CoF_RetailGiveItem( &clients[1], "inventoryitems/../config.cfg" ));
	assert( give_calls == 0 );
	/* Review Focus 5, pinned: a path escape never reaches the filesystem */
	assert( strstr( fs_paths, "inventoryitems/fuse.txt" ) && !strstr( fs_paths, ".." ));

	/* remove: the game's own ClientCommand, run as that player */
	assert( SV_CoF_RetailRemoveItem( &clients[1], "inventoryitems/hallkey.txt" ));
	assert( !strcmp( last_command, "inventorydeletenamed \"inventoryitems/hallkey.txt\" 0" ) && last_command_edict == clients[1].edict );
	last_command[0] = 0;
	assert( !SV_CoF_RetailRemoveItem( &clients[1], "x\";quit;\"" ) && !last_command[0] );
	/* in a phone call or on the phone screen the game would drop it: the engine's own command still lands, and the
	 * player is back in the call after it */
	{
		int screen = 2;
		private_data[1][0x1EE8] = 1; memcpy( private_data[1] + 0x1EA0, &screen, 4 );
		assert( SV_CoF_RetailRemoveItem( &clients[1], "inventoryitems/hallkey.txt" ));
		assert( command_saw_call == 0 && command_saw_screen == 0 );
		memcpy( &screen, private_data[1] + 0x1EA0, 4 ); assert( private_data[1][0x1EE8] == 1 && screen == 2 );
		assert( SV_CoF_RetailCommand( &clients[1], "flashlight" ) && command_saw_call == 0 && private_data[1][0x1EE8] == 1 );
		private_data[1][0x1EE8] = 0; screen = 0; memcpy( private_data[1] + 0x1EA0, &screen, 4 );
	}

	/* names */
	assert( SV_CoF_RetailItemName( "inventoryitems/hallkey.txt", nice, sizeof( nice )) && !strcmp( nice, "Hall key" ));
	assert( !SV_CoF_RetailItemName( "inventoryitems/fuse.txt", nice, sizeof( nice )) && !strcmp( nice, "item" ));

	/* ProFont: a colour byte, then the text with its terminator */
	assert( SV_CoF_RetailProFont( &clients[0], 2, "hi", false ));
	assert( !strcmp( last_msg_name, "ProFont" ) && last_msg_size == 4 && !memcmp( last_msg, "\x02hi", 4 ) && !last_msg_reliable );
	assert( !SV_CoF_RetailProFont( &clients[0], 2, "a DEADNOTIFY line", true )); /* the client would swap in its respawn prompt */
	assert( !SV_CoF_RetailProFont( &clients[0], 9, "hi", true ));

	/* ScreenFade: three x4096 shorts, little-endian, then rgba */
	assert( SV_CoF_RetailScreenFade( &clients[0], 0.5f, 0.1f, 0, 0, 0, 0, 255 ));
	assert( !strcmp( last_msg_name, "ScreenFade" ) && last_msg_size == 10 && !memcmp( last_msg, fade, 10 ) && last_msg_reliable );

	/* the document a player is reading: an EHANDLE (edict, serial) in game memory */
	{
		edict_t *doc = &edicts[3], *bogus = (edict_t *)( private_data[1] + 8 );
		int serial = 7;
		doc->v.classname = 4; doc->serialnumber = 7; doc->pvPrivateData = doc_private;
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* reading nothing */
		memcpy( private_data[0] + 0x630, &doc, sizeof( doc ));
		memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		assert( SV_CoF_RetailOpenDocument( &clients[0] ) == doc );
		serial = 8; memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* a stale handle */
		serial = 7; memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		memcpy( private_data[0] + 0x630, &bogus, sizeof( bogus ));
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* never follow a pointer outside the edict array */
		memcpy( private_data[0] + 0x630, &doc, sizeof( doc ));
		doc->v.classname = 0;
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* not a cof_document */
		doc->v.classname = 4;

		/* AddDocument: the game's own member, with the other player as this */
		assert( SV_CoF_RetailAddDocument( &clients[1], doc ));
		assert( calls == 1 && call_rva == 0xDA6D0 && call_self == private_data[1] && call_arg == doc_private );
		assert( !SV_CoF_RetailAddDocument( &clients[1], &edicts[2] ) && calls == 1 ); /* a player is not a document */
		assert( !SV_CoF_RetailAddDocument( &clients[1], NULL ) && calls == 1 );
		doc->v.flags = FL_KILLME;
		assert( !SV_CoF_RetailAddDocument( &clients[1], doc ) && calls == 1 ); /* being removed */
		doc->v.flags = 0;
	}

	/* a code note: the "nerds" placeholder in the first free slot, then AddNote with the code */
	{
		static const byte note[21] = { 2, 1, 0xD2, 0x04, 3, 'n', 'e', 'r', 'd', 's', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 0 );
		private_data[1][0x688 + 0x120 * 1 + 0x40] = 'x'; /* slot 1 is used */
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 1 );
		assert( SV_CoF_RetailCodeNote( &clients[1], 1234, 3 ));
		assert( !strcmp( last_msg_name, "AddNote" ) && last_msg_size == 21 && !memcmp( last_msg, note, 21 ) && last_msg_reliable );
		assert( !strcmp( (char *)private_data[1] + 0x688 + 0x120 * 2 + 0x40, "nerds" ));
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 2 );
		for( i = 1; i <= 16; i++ ) private_data[1][0x688 + 0x120 * i + 0x40] = 'x';
		assert( !SV_CoF_RetailCodeNote( &clients[1], 1234, 3 )); /* all sixteen slots are used */
		/* slot 16 is never filled by the engine: client.dll's notes array holds 1-15 (16 overwrites the next members) */
		private_data[1][0x688 + 0x120 * 16 + 0x40] = 0;
		assert( !SV_CoF_RetailCodeNote( &clients[1], 1234, 3 ) && !private_data[1][0x688 + 0x120 * 16 + 0x40] );
		assert( !SV_CoF_RetailArchiveNote( &clients[1], 16, note, sizeof( note )));
		assert( SV_CoF_RetailNoteSlot( &clients[1], "", 1 ) == 0 ); /* no free slot the engine may use */
		assert( !SV_CoF_RetailCodeNote( &clients[1], 70000, 3 )); /* the code travels as a short */
		memset( private_data[1] + 0x688, 0, 0x120 * 17 );
		clients[1].state = 2;
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == -1 && !SV_CoF_RetailCodeNote( &clients[1], 1, 0 ));
		clients[1].state = cs_spawned;
	}

	/* probe: give and remove need sv_cheats; slots and messages do not */
	argc = 4; argv[0] = "cof_story_probe"; argv[1] = "give"; argv[2] = "2"; argv[3] = "inventoryitems/hallkey.txt";
	give_calls = 0; probe();
	assert( give_calls == 0 && strstr( console, "what=give result=0 reason=needs-sv_cheats" ));
	cheats = 1; probe();
	assert( give_calls == 1 && strstr( console, "cofstory phase=probe what=give userid=2 result=1" ));
	argc = 3; argv[1] = "slots"; argv[2] = "1"; probe();
	assert( strstr( console, "cofstory phase=probe-slot userid=1 index=1 descriptor=\"inventoryitems/hallkey.txt\"" ));
	assert( strstr( console, "what=slots userid=1 result=1 free=4" ));
	argc = 5; argv[1] = "msg"; argv[3] = "4"; argv[4] = "hello"; probe();
	assert( last_msg[0] == 4 && !strcmp( (char *)last_msg + 1, "hello" ));
	/* probe: doc and codenote write game state, so they need sv_cheats too */
	argc = 3; argv[1] = "doc"; argv[2] = "2"; cheats = 0; calls = 0; probe();
	assert( calls == 0 && strstr( console, "what=doc result=0 reason=needs-sv_cheats" ));
	cheats = 1; probe();
	assert( calls == 1 && call_arg == doc_private && strstr( console, "what=doc userid=2 result=1 documents=0->0" ));
	argc = 4; argv[3] = "2"; probe(); /* the map has no second document */
	assert( calls == 1 && strstr( console, "what=doc userid=2 result=0 documents=0->0" ));
	argc = 5; argv[1] = "codenote"; argv[3] = "4321"; argv[4] = "1"; probe();
	assert( strstr( console, "what=codenote userid=2 result=1 documents=0->1" ));
	argc = 4; argv[1] = "fade"; argv[2] = "1"; argv[3] = "1.5"; probe();
	assert( last_msg[2] == 0x00 && last_msg[3] == 0x18 ); /* hold 1.5 s = 0x1800 */
	argc = 5; argv[1] = "msg"; argv[2] = "1"; argv[3] = "4"; argv[4] = "hello";
	argv[2] = "99"; probe();
	assert( strstr( console, "reason=no-such-player" ));
	gate = 0; argv[2] = "1"; probe();
	assert( strstr( console, "reason=story-off" ));
	/* The AI companion. Arms: the weapons whose m_pPlayer is this player, the held one read in full. */
	gate = 1; sv.time = 50.0; svgame.numEntities = 12;
	for( i = 4; i < 12; i++ ) { edicts[i].free = 1; edicts[i].pvPrivateData = NULL; }
	memset( private_data[0], 0, sizeof( private_data[0] ));
	{
		cof_retail_arms_t arms;
		weapon( 4, 0, 5, private_data[0], 3, 12, 2 ); /* glock, 12 loaded, glock ammo */
		weapon( 5, 1, 6, private_data[0], 6, -1, 0 ); /* switchblade */
		weapon( 6, 2, 8, private_data[1], 4, 5, 6 ); /* the other player's shotgun */
		weapon( 7, 3, 7, private_data[0], 8, -1, 10 ); /* syringe, none left */
		put_int( private_data[0] + 0x2540 + 2 * 4, 30 );
		put_pointer( private_data[0] + 0x252C, weapon_private[0] ); /* the glock in hand */
		put_float( weapon_private[0] + 0x1CC, 49.0f ); put_float( weapon_private[0] + 0x1D0, 49.0f );
		weapon_private[0][0x178] = 1;
		assert( SV_CoF_RetailArms( &clients[0], &arms ));
		assert( arms.weapon == COF_ARMS_GUN && arms.clip == 12 && arms.reserve == 30 && arms.ready && arms.sights && !arms.reloading && !arms.struggle );
		assert( arms.has_gun && !strcmp( arms.gun, "weapon_glock" ) && arms.has_melee && !strcmp( arms.melee, "weapon_switchblade" ));
		assert( !arms.has_heal ); /* no syringes left: nothing to heal with */
		/* between shots, mid-switch, reloading, struggling */
		put_float( weapon_private[0] + 0x1CC, 51.0f ); SV_CoF_RetailArms( &clients[0], &arms ); assert( !arms.ready );
		put_float( weapon_private[0] + 0x1CC, 49.0f ); private_data[0][0x1EF9] = 1; SV_CoF_RetailArms( &clients[0], &arms ); assert( !arms.ready );
		private_data[0][0x1EF9] = 0; put_float( private_data[0] + 0x308, 52.0f ); SV_CoF_RetailArms( &clients[0], &arms ); assert( !arms.ready );
		put_float( private_data[0] + 0x308, 0.0f );
		put_int( weapon_private[0] + 0x1F0, 1 ); put_int( weapon_private[0] + 0x140, 2 );
		SV_CoF_RetailArms( &clients[0], &arms ); assert( arms.reloading && arms.struggle );
		put_int( weapon_private[0] + 0x1F0, 0 ); put_int( weapon_private[0] + 0x140, 0 );
		/* a gun with no rounds at all is no gun: held, it counts as nothing; the knife is the weapon */
		put_int( weapon_private[0] + 0x1E4, 0 ); put_int( private_data[0] + 0x2540 + 2 * 4, 0 );
		SV_CoF_RetailArms( &clients[0], &arms ); assert( arms.weapon == COF_ARMS_NONE && !arms.has_gun && arms.has_melee );
		/* the best gun with rounds: a shotgun of its own beats the glock */
		weapon( 6, 2, 8, private_data[0], 4, 5, 6 );
		SV_CoF_RetailArms( &clients[0], &arms ); assert( arms.has_gun && !strcmp( arms.gun, "weapon_shotgun" ));
		/* Supply: the infinite-ammo switch, a magazine in reserve for each gun, a syringe for the syringe */
		SV_CoF_RetailSupply( &clients[0] );
		assert( private_data[0][0x21EA] == 1 && ammo( 0, 2 ) == 15 && ammo( 0, 6 ) == 5 && ammo( 0, 10 ) == 1 );
		SV_CoF_RetailArms( &clients[0], &arms ); assert( arms.has_heal && !strcmp( arms.heal, "weapon_syringe" ));
		put_int( private_data[0] + 0x2540 + 2 * 4, 40 ); SV_CoF_RetailSupply( &clients[0] ); assert( ammo( 0, 2 ) == 40 ); /* never taken away */
		/* Equip: the game's own SelectItem on this player, with the classname */
		calls = 0;
		assert( SV_CoF_RetailEquip( &clients[0], "weapon_shotgun" ) && calls == 1 && call_rva == 0xF1A50 && call_self == private_data[0] && !strcmp( call_arg, "weapon_shotgun" ));
		assert( !SV_CoF_RetailEquip( &clients[0], "item_key" ) && !SV_CoF_RetailEquip( &clients[0], NULL ) && calls == 1 );
		/* Dual wield (live 2026-10-15: nothing happened in co-op): the game builds weapon_dualweapon
		 * but its multiplayer rules never switch to it (weight 1); the engine switches, like single player */
		weapon( 8, 3, 13, private_data[0], 15, -1, 0 );
		put_int( weapon_private[3] + 0x204, 3 ); put_int( weapon_private[3] + 0x200, 1 ); /* glock left, flashlight right */
		put_int( private_data[0] + 0x1E70, 3 ); put_int( private_data[0] + 0x1E74, 1 );
		calls = 0;
		assert( SV_CoF_RetailDualDeploy( &clients[0] ) && calls == 1 && call_rva == 0xF5E50 && call_self == private_data[0] && call_arg == weapon_private[3] );
		/* already in hand, or already on its way: nothing */
		put_pointer( private_data[0] + 0x252C, weapon_private[3] ); calls = 0;
		assert( !SV_CoF_RetailDualDeploy( &clients[0] ) && !calls );
		put_pointer( private_data[0] + 0x252C, weapon_private[0] ); put_pointer( private_data[0] + 0x2538, weapon_private[3] );
		assert( !SV_CoF_RetailDualDeploy( &clients[0] ) && !calls );
		put_pointer( private_data[0] + 0x2538, NULL );
		/* a dual weapon of another pair (the command failed), or of the other player: not ours to deploy */
		put_int( private_data[0] + 0x1E70, 6 ); assert( !SV_CoF_RetailDualDeploy( &clients[0] ) && !calls );
		put_int( private_data[0] + 0x1E70, 3 ); put_pointer( weapon_private[3] + 0x160, private_data[1] );
		assert( !SV_CoF_RetailDualDeploy( &clients[0] ) && !calls );
		edicts[8].free = 1;
		/* the allocation check runs once per entity per map, not every frame */
		private_lookups = 0; SV_CoF_RetailArms( &clients[0], &arms ); SV_CoF_RetailArms( &clients[0], &arms );
		assert( private_lookups == 0 );
		svs.spawncount++; SV_CoF_RetailArms( &clients[0], &arms ); assert( private_lookups > 0 );
		edicts[4].serialnumber++; private_lookups = 0; SV_CoF_RetailArms( &clients[0], &arms ); assert( private_lookups == 1 );
		/* the same edict and serial with another allocation is checked again */
		edicts[5].pvPrivateData = weapon_private[2]; weapon( 6, 1, 6, private_data[0], 6, -1, 0 );
		private_lookups = 0; SV_CoF_RetailArms( &clients[0], &arms ); assert( private_lookups == 2 );
		/* The test gun. Carrying, by kind: 0 not carried, 1 carried, 2 in hand; -1 for no weapon we know */
		assert( SV_CoF_RetailCarrying( &clients[0], "weapon_glock" ) == 2 && SV_CoF_RetailCarrying( &clients[0], "weapon_shotgun" ) == 1 );
		assert( SV_CoF_RetailCarrying( &clients[0], "weapon_m16" ) == 0 && SV_CoF_RetailCarrying( &clients[1], "weapon_shotgun" ) == 0 );
		assert( SV_CoF_RetailCarrying( &clients[0], "weapon_famas" ) == -1 && SV_CoF_RetailCarrying( &clients[0], NULL ) == -1 );
		put_int( weapon_private[0] + 0x168, 9 ); /* an m16 in hand, whatever its classname says (aliases share the id) */
		assert( SV_CoF_RetailCarrying( &clients[0], "weapon_m16" ) == 2 && SV_CoF_RetailCarrying( &clients[0], "weapon_glock" ) == 0 );
		put_int( weapon_private[0] + 0x168, 3 );
		clients[0].state = 0; assert( SV_CoF_RetailCarrying( &clients[0], "weapon_glock" ) == -1 ); clients[0].state = cs_spawned;
		/* the infinite-ammo switch alone, both ways, saying what it was; a value that is no bool is not ours */
		assert( SV_CoF_RetailInfiniteAmmo( &clients[0], false ) == 1 && private_data[0][0x21EA] == 0 );
		assert( SV_CoF_RetailInfiniteAmmo( &clients[0], true ) == 0 && private_data[0][0x21EA] == 1 );
		assert( SV_CoF_RetailInfiniteAmmo( &clients[0], true ) == 1 && private_data[0][0x21EA] == 1 );
		private_data[0][0x21EA] = 7; assert( SV_CoF_RetailInfiniteAmmo( &clients[0], false ) == -1 && private_data[0][0x21EA] == 7 );
		private_data[0][0x21EA] = 1;
		clients[0].state = 0; assert( SV_CoF_RetailInfiniteAmmo( &clients[0], false ) == -1 && private_data[0][0x21EA] == 1 ); clients[0].state = cs_spawned;
		/* a phone call: m_bInPhoneCall */
		assert( !SV_CoF_RetailInPhoneCall( &clients[0] ));
		private_data[0][0x1EE8] = 1; assert( SV_CoF_RetailInPhoneCall( &clients[0] ) && !SV_CoF_RetailInPhoneCall( &clients[1] ));
		private_data[0][0x1EE8] = 0;
		/* the wheelchair: m_hWheelchair names a live entity */
		assert( !SV_CoF_RetailInWheelchair( &clients[0] ));
		edicts[8].free = 0; edicts[8].serialnumber = 4; handle( private_data[0] + 0x390, &edicts[8] );
		assert( SV_CoF_RetailInWheelchair( &clients[0] ) && !SV_CoF_RetailInWheelchair( &clients[1] ));
		edicts[8].serialnumber++; assert( !SV_CoF_RetailInWheelchair( &clients[0] )); /* the chair is gone */
		handle( private_data[0] + 0x390, NULL ); edicts[8].free = 1;
	}
	/* A trigger_camera holds the view: m_hCurrentCamera names a live entity and the view is not the player's own. */
	{
		int view = 8;
		assert( !SV_CoF_RetailCameraHolds( &clients[0] ));
		edicts[8].free = 0; edicts[8].serialnumber = 4;
		handle( private_data[0] + 0x2348, &edicts[8] ); memcpy( private_data[0] + 0x2038, &view, 4 );
		assert( SV_CoF_RetailCameraHolds( &clients[0] ) && !SV_CoF_RetailCameraHolds( &clients[1] ));
		view = NUM_FOR_EDICT( clients[0].edict ); memcpy( private_data[0] + 0x2038, &view, 4 );
		assert( !SV_CoF_RetailCameraHolds( &clients[0] )); /* back to the player's own view */
		view = 8; memcpy( private_data[0] + 0x2038, &view, 4 ); edicts[8].serialnumber++;
		assert( !SV_CoF_RetailCameraHolds( &clients[0] )); /* the camera is gone */
		view = 0; memcpy( private_data[0] + 0x2038, &view, 4 ); handle( private_data[0] + 0x2348, NULL ); edicts[8].free = 1;
	}
	/* Night vision on F: only for its owner (m_bHasNightVision), with the game's own gates (1001C1FB): a scene's
	 * camera that holds the view, an attached camera, the wheelchair. The request goes in m_iHeadShieldOn. */
	{
		int state = 0, view = 0; float next = 0.0f;
		assert( SV_CoF_RetailNightVision( &clients[0] ) == -1 ); /* not owned */
		private_data[0][0x434] = 1; memcpy( private_data[0] + 0x2300, &state, 4 ); memcpy( private_data[0] + 0x3EC, &next, 4 );
		assert( SV_CoF_RetailNightVision( &clients[0] ) == 1 ); memcpy( &state, private_data[0] + 0x2300, 4 ); assert( state == 101 );
		assert( SV_CoF_RetailNightVision( &clients[0] ) == 0 ); /* a switch under way */
		state = 1; memcpy( private_data[0] + 0x2300, &state, 4 );
		edicts[8].free = 0; edicts[8].serialnumber = 4;
		handle( private_data[0] + 0x390, &edicts[8] ); assert( SV_CoF_RetailNightVision( &clients[0] ) == 0 ); /* the wheelchair */
		handle( private_data[0] + 0x390, NULL );
		handle( private_data[0] + 0x388, &edicts[8] ); assert( SV_CoF_RetailNightVision( &clients[0] ) == 0 ); /* an attached camera */
		handle( private_data[0] + 0x388, NULL );
		handle( private_data[0] + 0x2348, &edicts[8] ); view = 8; memcpy( private_data[0] + 0x2038, &view, 4 );
		assert( SV_CoF_RetailNightVision( &clients[0] ) == 0 ); /* the scene's camera holds the view */
		view = NUM_FOR_EDICT( clients[0].edict ); memcpy( private_data[0] + 0x2038, &view, 4 );
		assert( SV_CoF_RetailNightVision( &clients[0] ) == 1 ); /* a camera that no longer holds the view */
		memcpy( &state, private_data[0] + 0x2300, 4 ); assert( state == 100 );
		next = (float)sv.time + 1.0f; memcpy( private_data[0] + 0x3EC, &next, 4 ); state = 0; memcpy( private_data[0] + 0x2300, &state, 4 );
		assert( SV_CoF_RetailNightVision( &clients[0] ) == 0 ); /* the game's delay */
		memcpy( &state, private_data[0] + 0x2300, 4 ); assert( state == 0 );
		handle( private_data[0] + 0x2348, NULL ); view = 0; memcpy( private_data[0] + 0x2038, &view, 4 );
		next = 0.0f; memcpy( private_data[0] + 0x3EC, &next, 4 ); private_data[0][0x434] = 0; edicts[8].free = 1;
	}

	/* CPR: player 2 dies on the ground; player 1 can start the game's CPR on them (Use, body first). */
	{
		edicts[1].v.health = 100; edicts[1].v.deadflag = DEAD_NO;
		edicts[2].v.health = 0; edicts[2].v.deadflag = DEAD_DEAD; edicts[2].v.flags = FL_ONGROUND;
		memset( private_data[1], 0, sizeof( private_data[1] ));
		assert( SV_CoF_RetailRevivable( &clients[1] ) && !SV_CoF_RetailRevivable( &clients[0] ));
		edicts[2].v.flags = 0; assert( !SV_CoF_RetailRevivable( &clients[1] )); edicts[2].v.flags = FL_ONGROUND; /* still falling */
		private_data[1][0x501] = 1; assert( !SV_CoF_RetailRevivable( &clients[1] )); private_data[1][0x501] = 0; /* gibbed */
		private_data[1][0x4E8] = 1; assert( !SV_CoF_RetailRevivable( &clients[1] )); private_data[1][0x4E8] = 0; /* a restart is pending */
		uses = 0;
		edicts[2].v.fuser3 = 3.0f; assert( !SV_CoF_RetailRevivable( &clients[1] )); edicts[2].v.fuser3 = 0.0f; /* in its get-up */
		edicts[1].v.fuser3 = 5.0f; assert( !SV_CoF_RetailRevive( &clients[0], &clients[1] ) && !uses ); edicts[1].v.fuser3 = 0.0f; /* the reviver is busy */
		edicts[2].v.flags = 0; assert( !SV_CoF_RetailRevive( &clients[0], &clients[1] ) && !uses ); edicts[2].v.flags = FL_ONGROUND;
		assert( SV_CoF_RetailRevive( &clients[0], &clients[1] ) && uses == 1 && used_body == &edicts[2] && used_by == &edicts[1] );
		assert( SV_CoF_RetailReviving( &clients[0] ) && SV_CoF_RetailRevived( &clients[1] ) && !SV_CoF_RetailRevivable( &clients[1] ));
		assert( !SV_CoF_RetailRevive( &clients[0], &clients[1] ) && uses == 1 ); /* already at it */
		/* the reviver dies mid-CPR: the body's link to them is cleared, so a later CPR can start */
		SV_CoF_RetailCprRepair(); assert( SV_CoF_RetailRevived( &clients[1] )); /* alive and linked: left alone */
		edicts[1].v.health = 0; edicts[1].v.deadflag = DEAD_DEAD;
		console_length = 0; console[0] = 0;
		SV_CoF_RetailCprRepair();
		assert( !SV_CoF_RetailRevived( &clients[1] ) && strstr( console, "cofstory phase=cpr-unlinked userid=2" ));
		/* a link whose reviver went on to something else is cleared too */
		edicts[1].v.health = 100; edicts[1].v.deadflag = DEAD_NO;
		handle( private_data[1] + 0x26A0, &edicts[1] ); handle( private_data[0] + 0x2698, NULL );
		SV_CoF_RetailCprRepair(); assert( !SV_CoF_RetailRevived( &clients[1] ));
		/* a reviver left in its CPR state with no body (the game cancelled when the body came alive)
		 * gets up the game's own way; one still linked is left alone */
		edicts[1].v.fuser3 = 2.0f; console_length = 0; console[0] = 0;
		SV_CoF_RetailCprRepair();
		{
			float end;
			memcpy( &end, private_data[0] + 0x4FC, sizeof( end ));
			assert( edicts[1].v.fuser3 == 4.0f && end == 50.0f && strstr( console, "cofstory phase=cpr-unstuck userid=1" ));
		}
		edicts[1].v.fuser3 = 2.0f; handle( private_data[0] + 0x2698, &edicts[2] );
		SV_CoF_RetailCprRepair(); assert( edicts[1].v.fuser3 == 2.0f );
		edicts[1].v.fuser3 = 0.0f; handle( private_data[0] + 0x2698, NULL );
		/* A syringe heal holds both players at fuser3 25 with the view locked until its BladeRetract 1.5 s later;
		 * a healer killed in between leaves the partner held for good. After 3 s it is let go, view unlocked. */
		edicts[2].v.health = 100; edicts[2].v.deadflag = DEAD_NO; handle( private_data[1] + 0x26A0, NULL );
		edicts[2].v.fuser3 = 25.0f; console_length = 0; console[0] = 0; last_msg_name[0] = 0;
		SV_CoF_RetailCprRepair(); assert( edicts[2].v.fuser3 == 25.0f );           /* a heal under way */
		sv.time += 2.0; SV_CoF_RetailCprRepair(); assert( edicts[2].v.fuser3 == 25.0f );
		sv.time += 1.5; SV_CoF_RetailCprRepair();
		assert( edicts[2].v.fuser3 == 0.0f && strstr( console, "cofstory phase=syringe-unstuck userid=2" ));
		assert( !strcmp( last_msg_name, "ViewLock" ) && last_msg_size == 3 && !last_msg[0] && !last_msg[1] && !last_msg[2] );
		/* a heal that ends on time is not touched */
		edicts[2].v.fuser3 = 25.0f; SV_CoF_RetailCprRepair(); sv.time += 1.5; edicts[2].v.fuser3 = 0.0f; SV_CoF_RetailCprRepair();
		edicts[2].v.fuser3 = 25.0f; last_msg_name[0] = 0; SV_CoF_RetailCprRepair(); sv.time += 2.0; SV_CoF_RetailCprRepair();
		assert( edicts[2].v.fuser3 == 25.0f && !last_msg_name[0] );             /* a new heal: its own 3 s */
		edicts[2].v.fuser3 = 0.0f; SV_CoF_RetailCprRepair();
		/* the dead never revive anyone */
		edicts[1].v.health = 0; assert( !SV_CoF_RetailRevive( &clients[0], &clients[1] ) && uses == 1 );
		edicts[1].v.health = 100;
		/* a hard landing: held down while fuser3 is set */
		private_data[0][0x1ED0] = 1; assert( !SV_CoF_RetailFallen( &clients[0] ));
		edicts[1].v.fuser3 = 1.0f; assert( SV_CoF_RetailFallen( &clients[0] ));
		edicts[1].v.fuser3 = 0.0f; private_data[0][0x1ED0] = 0;
		/* the game's own client commands */
		assert( SV_CoF_RetailCommand( &clients[0], "flashlight" ) && !strcmp( last_command, "flashlight" ) && last_command_edict == &edicts[1] );
	}

	/* Hostility: the game's own relationship table, row the monster's class, column the player. */
	{
		int relation;
		memset( dll_image, 0, sizeof( dll_image ));
		relation = 1; memcpy( dll_image + 0x217118 + ( 7 * 19 + 2 ) * 4, &relation, 4 ); /* alien monsters dislike players */
		relation = 2; memcpy( dll_image + 0x217118 + ( 5 * 19 + 2 ) * 4, &relation, 4 ); /* nerds hate them */
		relation = -2; memcpy( dll_image + 0x217118 + ( 3 * 19 + 2 ) * 4, &relation, 4 ); /* passive humans are allies */
		monster( 8, 0, 9, 0, 0 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 ); /* a slower: class 7 by default */
		monster( 8, 0, 12, 0, 3 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 ); /* the nerd's class is fixed: the key is ignored */
		monster( 8, 0, 11, 0, 0 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* class 18: its row says nothing about players here */
		monster( 8, 0, 9, 0, 3 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* c_start2's chainsaw actor: m_iClass 3 */
		monster( 8, 0, 9, 1, 0 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* m_iPlayerReact 1: never */
		put_int( monster_private[0] + 0x2EC, 1 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* not even provoked */
		monster( 8, 0, 9, 4, 3 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 ); /* 4: always */
		monster( 8, 0, 9, 2, 0 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* 2: only once provoked */
		put_int( monster_private[0] + 0x2EC, 1 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 );
		monster( 8, 0, 10, 0, 0 ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* monster_generic: allies, props, boss health */
		monster( 8, 0, 9, 0, 0 ); edicts[8].v.spawnflags = 16; assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); /* a prisoner */
		/* waiting for its script: left alone until it goes for a player; then first in line */
		edicts[8].v.spawnflags = 128; assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 );
		handle( monster_private[0] + 0x184, &edicts[2] ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 2 );
		edicts[8].v.spawnflags = 0; handle( monster_private[0] + 0x184, &edicts[5] ); assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 ); /* after a weapon? not a player */
		edicts[2].serialnumber++; handle( monster_private[0] + 0x184, &edicts[2] ); edicts[2].serialnumber--;
		assert( SV_CoF_RetailHostile( &edicts[8] ) == 1 ); /* a stale handle */
		gate = 0; assert( SV_CoF_RetailHostile( &edicts[8] ) == 0 ); gate = 1; /* no verified game: no table to read */
		assert( SV_CoF_RetailHostile( NULL ) == 0 );
	}

	puts( "retail scenarios passed" );
	return 0;
}
