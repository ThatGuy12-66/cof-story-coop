#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_state_data.h"

int main( void )
{
	cof_state_data_t party = {0};
	cof_story_values_t host = {0}, guest = {0}, previous;
	unsigned char code[] = { 1, 1, 0x39, 0x30, 2, 'n','e','r','d','s',0,0,0,0,0,0,0,0,0,0,0 };
	unsigned char document[] = { 5, 0, 0, 0, 0, 'a','.','t','x','t',0,0,0,0,0,0,0,0,0,'A',0,0 };
	unsigned char invalid[sizeof( code )];
	host.code = 2793; host.code_type = 1;
	host.digits[0] = 1; host.digits[1] = 2; host.digits[2] = 3; host.digits[3] = 4;
	strcpy( host.objective, "objective_b" );
	CoF_StateObserve( &party, &host, NULL );
	assert( party.values.code == 2793 && party.values.digits[3] == 4 );
	guest.code = 0; guest.digits[0] = 9;
	CoF_StateObserve( &party, &guest, NULL );
	assert( party.values.code == 2793 && party.values.digits[0] == 1 );
	previous = party.values; guest = previous; guest.code = 9999;
	CoF_StateObserve( &party, &guest, &previous );
	assert( party.values.code == 9999 );
	previous = party.values; guest = previous; guest.code = 5247; guest.code_type = 2;
	CoF_StateObserve( &party, &guest, &previous );
	assert( party.values.code == 5247 && party.values.code_type == 2 );
	guest.ending[1] = 1; guest.three_slots = 1;
	CoF_StateObserve( &party, &guest, &previous );
	memset( &guest, 0, sizeof( guest ));
	CoF_StateObserve( &party, &guest, NULL );
	assert( party.values.ending[1] && party.values.three_slots );
	assert( !strcmp( party.values.objective, "objective_b" ));
	assert( CoF_StateNote( &party, code, sizeof( code )) == 0 );
	code[0] = 9;
	assert( CoF_StateNote( &party, code, sizeof( code )) == 0 && party.note_count == 1 );
	code[2] = 0x40;
	assert( CoF_StateNote( &party, code, sizeof( code )) == 1 && party.note_count == 2 );
	assert( CoF_StateNote( &party, document, sizeof( document )) == 2 );
	document[0] = 2;
	assert( CoF_StateNote( &party, document, sizeof( document )) == 2 && party.note_count == 3 );
	memcpy( invalid, code, sizeof( code )); invalid[sizeof( invalid ) - 1] = 'x';
	assert( CoF_StateNote( &party, invalid, sizeof( invalid )) == -1 );
	assert( CoF_StateNote( &party, code, 4 ) == -1 );
	assert( party.note_count == 3 );
	/* The P345 answer (doctor weapon) is the party's, whoever gave it: the c_attic
	 * doctor reads "the first player" and never fights while that is 0. */
	{
		cof_state_data_t story = {0};
		cof_story_values_t a = {0}, b = {0}, was;
		CoF_StateObserve( &story, &a, NULL ); CoF_StateObserve( &story, &b, NULL );
		assert( story.values.doctor_weapon == 0 );
		was = b; b.doctor_weapon = 1; CoF_StateObserve( &story, &b, &was );
		assert( story.values.doctor_weapon == 1 );
		/* a joiner or rejoined player with 0 never undoes it; neither does an unchanged player */
		memset( &a, 0, sizeof( a )); CoF_StateObserve( &story, &a, NULL );
		was = a; CoF_StateObserve( &story, &a, &was );
		assert( story.values.doctor_weapon == 1 );
		/* a player arriving holding the other answer (an old rejoin snapshot) does not flip it */
		a.doctor_weapon = 2; CoF_StateObserve( &story, &a, NULL ); assert( story.values.doctor_weapon == 1 );
		/* a later real change (the revolver path written over it) is followed */
		was = b; b.doctor_weapon = 2; CoF_StateObserve( &story, &b, &was );
		assert( story.values.doctor_weapon == 2 );
		/* junk values are ignored */
		was = b; b.doctor_weapon = 7; CoF_StateObserve( &story, &b, &was );
		assert( story.values.doctor_weapon == 2 );
		/* cof_begingame's stats: the first player who has them gives them to the party */
		memset( &a, 0, sizeof( a )); a.begin[0] = 1; a.begin[4] = 2; CoF_StateObserve( &story, &a, NULL );
		assert( story.values.begin[0] == 1 && story.values.begin[4] == 2 );
		memset( &b, 0, sizeof( b )); b.begin[0] = 1; b.begin[4] = 3; CoF_StateObserve( &story, &b, NULL );
		assert( story.values.begin[4] == 2 ); /* the first one stays */
	}
	puts( "shared state scenarios passed" );
	return 0;
}
