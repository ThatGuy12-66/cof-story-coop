/* sv_cof_arrival.h: the co-op torch kept across what the game does to frags, ScoreInfo, and which exit touches count. */
#include <stdio.h>
#include "arrival_under_test.h"

static void touch( const char *label, double door_age, int stayed, double arrived_age, int back )
{
	printf( "%s: %s\n", label, CoF_ArrivalTouchCounts( door_age, stayed, arrived_age, back ) ? "counts" : "refused" );
}

int main( void )
{
	unsigned char p[COF_ARRIVAL_SCOREINFO_SIZE];
	int i;
	printf( "F sees on=%d off=%d\n", CoF_TorchBefore( 1 ), CoF_TorchBefore( 0 ));
	printf( "after on->off %d, off->on %d, refused %d\n", CoF_TorchAfter( 32, 0 ), CoF_TorchAfter( 0, 32 ), CoF_TorchAfter( 32, 32 ));
	printf( "keep on frags=2 -> %d, on frags=34 -> %d, off frags=2 -> %d, on frags=0 -> %d\n",
		CoF_TorchKeep( 1, 2 ), CoF_TorchKeep( 1, 34 ), CoF_TorchKeep( 0, 2 ), CoF_TorchKeep( 1, 0 ));
	CoF_ScoreInfoPayload( p, 2, 34, -1, 0, 300 );
	printf( "scoreinfo" );
	for( i = 0; i < COF_ARRIVAL_SCOREINFO_SIZE; i++ ) printf( " %02x", p[i] );
	printf( " fields %d %d %d %d\n", CoF_ScoreInfoField( p, 0 ), CoF_ScoreInfoField( p, 1 ), CoF_ScoreInfoField( p, 2 ), CoF_ScoreInfoField( p, 3 ));
	touch( "restored in the exit back", -1.0, 1, 0.5, 1 );          // cm_park2 at sign-on, 23:00:24
	touch( "dodged into the exit back", -1.0, 0, 8.0, 1 );          // cm_park3, 23:00:18
	touch( "through the door back", 0.2, 1, 8.0, 1 );              // the door is how the story takes it
	touch( "walked back later", -1.0, 0, 20.0, 1 );
	touch( "another exit at once", -1.0, 0, 2.0, 0 );
	touch( "stood in another exit since arriving", -1.0, 1, 40.0, 0 );
	touch( "door long ago", 10.0, 0, 5.0, 1 );
	touch( "no story arrival", -1.0, 0, -1.0, 1 );
	/* quick slots: a weapon in an inventory slot 1..12; empty, cleared (cof_clearitems empties the names) or not a weapon: none */
	printf( "quick: glock 3 %d, slot 12 %d, slot 0 %d, slot 13 %d, cleared %d, key %d, none %d\n",
		CoF_QuickSlotShown( 3, "weapon_glock" ), CoF_QuickSlotShown( 12, "weapon_flashlight" ), CoF_QuickSlotShown( 0, "weapon_glock" ),
		CoF_QuickSlotShown( 13, "weapon_glock" ), CoF_QuickSlotShown( 3, "" ), CoF_QuickSlotShown( 3, "item_key" ), CoF_QuickSlotShown( 3, 0 ));
	return 0;
}
