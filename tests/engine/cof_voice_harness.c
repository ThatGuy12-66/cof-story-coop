/* sv_cof_voice.h: the game's multiplayer (police) voice samples and Simon's single-player ones. */
#include <stdio.h>
#include "voice_under_test.h"

static void map( const char *sample, int roll )
{
	char out[64];
	int set = CoF_VoiceSet( sample );
	if( set < 0 ) { printf( "%s: none\n", sample ); return; }
	CoF_VoiceName( set, roll % CoF_VoiceCount( set ), out, sizeof( out ));
	printf( "%s roll %d: %s\n", sample, roll, out );
}

int main( void )
{
	int set, n;
	char out[64];
	map( "player/police/Pain/Pain3.wav", 0 );
	map( "player/police/Pain/Pain10.wav", 15 );
	map( "player/police/LowHealth/lhealth4.wav", 4 );
	map( "player/police/Swing/Swing6.wav", 9 );
	map( "player/police/death/death2.wav", 6 );      /* the game's names differ in case from the files */
	map( "player/police/MorphineInjection/Morphine1.wav", 7 );
	map( "player/police/player_out_of_breath2.wav", 3 );
	map( "player/police/sprint_breathe9.wav", 4 );
	map( "player/police/adlib/help_1.wav", 0 );      /* multiplayer voice commands: Simon has none */
	map( "player/police/police_pager.wav", 0 );
	map( "player/police/Pain/Pain.wav", 0 );
	map( "player/police/Pain/Pain1.wavx", 0 );
	map( "player/vocal/Pain/Pain1.wav", 0 );         /* already Simon's */
	map( "", 0 );
	for( set = 0; set < CoF_VoiceSets(); set++ )
		for( n = 0; n < CoF_VoiceCount( set ); n++ )
		{
			CoF_VoiceName( set, n, out, sizeof( out ));
			printf( "name %s\n", out );
		}
	return 0;
}
