/* sv_cof_cheats.c's latch per client slot (CoF_Latch), against offline doubles: no game DLL. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define MAX_CLIENTS 4
typedef struct { int userid; } sv_client_t;
static sv_client_t clients[2];
static struct { int maxclients; sv_client_t *clients; } svs = { 2, clients };
static int CoF_ClientSlot( const sv_client_t *cl ) { int slot = (int)( cl - svs.clients ); return slot >= 0 && slot < svs.maxclients ? slot : -1; }
#include "latch_under_test.c"

int main( void )
{
	cof_latch_t *l;
	clients[0].userid = 1; clients[1].userid = 7;
	l = CoF_Latch( &clients[1] ); l->noclip = l->notarget = l->infammo = 1;
	/* the same person, frame after frame and through level changes (the userid stays): kept */
	l = CoF_Latch( &clients[1] ); assert( l->noclip && l->notarget && l->infammo );
	assert( !CoF_Latch( &clients[0] )->noclip ); /* another slot's own */
	/* the person leaves and someone else (a joiner, a bot, the same person reconnecting) gets the slot */
	clients[1].userid = 8;
	l = CoF_Latch( &clients[1] ); assert( !l->noclip && !l->notarget && !l->infammo && l->userid == 8 );
	l->fly = 1; assert( CoF_Latch( &clients[1] )->fly );
	puts( "cheat latch scenarios passed" );
	return 0;
}
