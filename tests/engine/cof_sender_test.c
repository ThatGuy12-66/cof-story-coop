/* Who "the first player" is while one player's client command runs: the pure
 * order, then the engine hook cut out of sv_game.c against small doubles. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 32
#define Q_strcmp strcmp
typedef struct edict_s { int free; struct { int classname; } v; } edict_t;
static edict_t entities[8];
static struct { int maxclients; } svs;
static int spawned[8];
#define NUM_FOR_EDICT( e ) ((int)(( e ) - entities ))
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static void *SV_ClientFromEdict( const edict_t *e, qboolean spawned_only ) { return spawned[e - entities] ? (void *)e : NULL; }
static const char *SV_GetString( int s ) { return s == 1 ? "player" : "other"; }
static int story = 1;
static qboolean SV_CoF_StoryCoopEnabled( void ) { return story; }

#include "sv_cof_sender.h"
#include "sender_under_test.c"

int main( void )
{
	static const unsigned char both[3] = { 0, 1, 1 }, three[4] = { 0, 1, 1, 1 }, gap[5] = { 0, 1, 0, 1, 1 };
	int e, i;

	/* the guest (edict 2) sent the command: it is "the first player" */
	assert( CoF_SenderNextPlayer( 0, 2, 2, both ) == 2 );
	/* a loop over every player visits the sender, then the rest, then stops */
	assert( CoF_SenderNextPlayer( 2, 2, 2, both ) == 1 && CoF_SenderNextPlayer( 1, 2, 2, both ) == 0 );
	/* Review Focus 5: the host's own commands keep the plain order */
	assert( CoF_SenderNextPlayer( 0, 1, 2, both ) == 1 && CoF_SenderNextPlayer( 1, 1, 2, both ) == 2 && CoF_SenderNextPlayer( 2, 1, 2, both ) == 0 );
	/* three players, the middle one sends: 2, 1, 3, end; nobody twice */
	assert( CoF_SenderNextPlayer( 0, 2, 3, three ) == 2 && CoF_SenderNextPlayer( 2, 2, 3, three ) == 1 );
	assert( CoF_SenderNextPlayer( 1, 2, 3, three ) == 3 && CoF_SenderNextPlayer( 3, 2, 3, three ) == 0 );
	/* empty client slots are skipped */
	assert( CoF_SenderNextPlayer( 4, 4, 4, gap ) == 1 && CoF_SenderNextPlayer( 1, 4, 4, gap ) == 3 && CoF_SenderNextPlayer( 3, 4, 4, gap ) == 0 );

	/* the engine hook: two players in play, one item entity after them */
	svs.maxclients = 2;
	for( i = 1; i <= 2; i++ ) { entities[i].v.classname = 1; spawned[i] = 1; }
	/* outside a command nothing changes */
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	SV_CoF_CommandSender( &entities[2] );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	e = 2; assert( CoF_SenderFind( "classname", "player", &e ) && e == 1 );
	/* after the last player the plain search goes on past the clients */
	e = 1; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	/* only "the player" lookups are touched */
	e = 0; assert( !CoF_SenderFind( "targetname", "player", &e ) && e == 0 );
	e = 0; assert( !CoF_SenderFind( "classname", "item_key", &e ) && e == 0 );
	/* a search that already left the clients behind is left alone */
	e = 5; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 5 );
	/* Review Focus 5: a sender whose edict is not in play changes nothing */
	spawned[2] = 0;
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	spawned[2] = 1;
	SV_CoF_CommandSender( NULL );
	/* The player whose movement command runs is first, unless the story layer
	 * prefers someone (the wheelchair's rider); a client command beats both. */
	SV_CoF_UsercmdSender( &entities[2] );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	SV_CoF_PreferFirst( &entities[1] );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 1 );
	SV_CoF_CommandSender( &entities[2] );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	SV_CoF_CommandSender( NULL ); SV_CoF_PreferFirst( NULL );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	/* with the story layer off, nothing is reordered */
	story = 0; e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 ); story = 1;
	SV_CoF_UsercmdSender( NULL );
	SV_CoF_CommandSender( NULL );
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	puts( "sender scenarios passed" );
	return 0;
}
