/* sv_cof_globals.h: hl.dll's single globals that hold one player's state, kept per player. */
#include <stdio.h>
#include "globals_under_test.h"

static unsigned char image[0x240000];
#define DUAL_THINK_STATE ( 0x2119D8 ) /* gDualWeapon.m_iThinkState: the left hand's pending action */
static unsigned char slots[3][COF_GLOBALS_BYTES];

static void show( const char *what )
{
	int i, off = 0;
	printf( "%s:", what );
	for( i = 0; i < COF_GLOBALS; off += cof_player_globals[i].size, i++ )
	{
		int value = 0;
		if( cof_player_globals[i].size > 4 ) memcpy( &value, image + DUAL_THINK_STATE, 4 ); /* the object: its pending action */
		else memcpy( &value, image + cof_player_globals[i].rva, cof_player_globals[i].size );
		printf( " %s=%d", cof_player_globals[i].name, value );
	}
	printf( "\n" );
}

static void command( int player, void ( *game )( void ))
{
	CoF_GlobalsLoad( image, slots[player] );
	game();
	CoF_GlobalsSave( image, slots[player] );
	CoF_GlobalsLoad( image, slots[0] ); /* outside a command: the host's */
}

static void host_trips( void ) { image[0x226489] = 1; image[0x226477] = 1; }
static void guest_moves( void ) { image[0x231F0A] = 1; image[0x231F5C] = 3; image[DUAL_THINK_STATE] = 8; /* phone light on */ image[0x226718] = 2; /* quick slot 2 pressed */ }
static void guest_reads( void ) { show( "the guest's own command" ); }

int main( void )
{
	int i;
	for( i = 0; i < COF_GLOBALS; i++ )
		printf( "global %s %X %d\n", cof_player_globals[i].name, cof_player_globals[i].rva, cof_player_globals[i].size );
	printf( "bytes %d\n", COF_GLOBALS_BYTES );
	command( 0, host_trips );
	show( "after the host's command" );
	command( 1, guest_moves );
	show( "after the guest's command" );
	command( 1, guest_reads );
	printf( "room_type owner: none %d, guest %d, host %d, host without the cvar %d, other cvar %d\n",
		CoF_RoomTypeOwner( "room_type", -1, 0, 1 ), CoF_RoomTypeOwner( "room_type", 1, 0, 1 ),
		CoF_RoomTypeOwner( "room_type", 0, 1, 1 ), CoF_RoomTypeOwner( "room_type", 0, 1, 0 ),
		CoF_RoomTypeOwner( "v_dark", 1, 0, 1 ));
	return 0;
}
