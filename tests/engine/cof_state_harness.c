#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wcomment" /* cc on Linux: a comment in sv_cof_descriptor.h holds a glob */
#endif
#define main retail_fixture_main
#include "retail_fixture.c"
#undef main
#define MAX_CLIENTS 32
#define FCL_FAKECLIENT 128
#define DEAD_NO 0
#define cs_connected 2
#define Q_stricmp _stricmp
static sv_client_t *SV_ClientFromEdict( edict_t *entity, qboolean spawned )
{
	int index;
	for( index = 0; index < 2; index++ ) if( clients[index].edict == entity ) return &clients[index];
	return NULL;
}
#include "state_under_test.c"

int main( void )
{
	cof_story_values_t values = {0}, observed;
	unsigned char note[] = { 3, 1, 0xD2, 4, 3, 'n','e','r','d','s',0,0,0,0,0,0,0,0,0,0,0 };
	retail_fixture_main(); gate = 1;
	memset( private_data, 0, sizeof( private_data ));
	clients[0].edict->v.health = clients[1].edict->v.health = 100;
	values.code = 2793; values.code_type = 1;
	values.digits[0] = 1; values.digits[1] = 2; values.digits[2] = 3; values.digits[3] = 4;
	assert( SV_CoF_RetailStoryWrite( &clients[0], &values ));
	SV_CoF_StateReset(); SV_CoF_StateFrame();
	assert( SV_CoF_RetailStoryRead( &clients[1], &observed ) && observed.code == 2793 && observed.digits[3] == 4 );
	observed.code = 9999; SV_CoF_RetailStoryWrite( &clients[1], &observed );
	SV_CoF_StateFrame();
	assert( SV_CoF_RetailStoryRead( &clients[0], &observed ) && observed.code == 9999 );
	SV_CoF_StateMessage( clients[0].edict, "AddNote", note, sizeof( note ));
	assert( SV_CoF_RetailArchiveNote( &clients[0], 3, note, sizeof( note )));
	SV_CoF_StateFrame();
	assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 1 );
	assert( last_msg[1] == 1 && last_msg[2] == 0xD2 );
	note[1] = note[2] = note[3] = note[4] = 0;
	SV_CoF_StateMessage( clients[0].edict, "AddNote", note, sizeof( note ));
	assert( note[1] == 1 && note[2] == 0xD2 && note[3] == 4 && note[4] == 3 );
	clients[1].userid = 9; memset( private_data[1], 0, sizeof( private_data[1] ));
	SV_CoF_StateFrame();
	assert( SV_CoF_RetailStoryRead( &clients[1], &observed ) && observed.code == 9999 );
	assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 1 );
	/* The guest answered the P345 question: cof_doctorweaponset wrote the guest's
	 * m_iDoctorWeapon (+0x5B0). The host, whom the c_attic doctor reads, gets it. */
	{
		int weapon = 1;
		memcpy( private_data[1] + 0x5B0, &weapon, sizeof( weapon ));
		SV_CoF_StateFrame();
		memcpy( &weapon, private_data[0] + 0x5B0, sizeof( weapon )); assert( weapon == 1 );
		/* a rejoined guest (fresh 0) does not undo it, and gets it back */
		clients[1].userid = 10; memset( private_data[1] + 0x5B0, 0, 4 );
		SV_CoF_StateFrame();
		memcpy( &weapon, private_data[0] + 0x5B0, sizeof( weapon )); assert( weapon == 1 );
		memcpy( &weapon, private_data[1] + 0x5B0, sizeof( weapon )); assert( weapon == 1 );
		/* cof_begingame's block (+0x678): copied to a player who lacks it, never over one who has it */
		private_data[0][0x678] = 1; private_data[0][0x67C] = 2;
		SV_CoF_StateFrame();
		assert( private_data[1][0x678] == 1 && private_data[1][0x67C] == 2 );
		private_data[1][0x67C] = 3; SV_CoF_StateFrame(); assert( private_data[1][0x67C] == 3 );
	}
	/* cof_objective writes FIRST's text and custom file; FIRST's PostThink sends Objectives( key, file, isNew ) and
	 * zeroes the file before the engine looks. The party's file comes from that message; a partner gets text and file
	 * once (their PostThink sends and zeroes it); nobody's file is written again after it was delivered (the game
	 * saves that field as a time). */
	{
		string_t key = SV_AllocString( "findthekey" ), none = 0, file;
		static const byte message[] = "findthekey\0txtfiles/custom_objectives.txt\0\1";
		static const byte plain[] = "findthekey\0txtfiles/hints.txt\0\0";
		memcpy( private_data[0] + 0x1E90, &key, 4 ); memcpy( private_data[0] + 0x420, &none, 4 );
		SV_CoF_StateMessage( clients[0].edict, "Objectives", (byte *)message, sizeof( message ) - 1 );
		SV_CoF_StateFrame();
		memcpy( &file, private_data[1] + 0x420, 4 ); assert( !strcmp( SV_GetString( file ), "txtfiles/custom_objectives.txt" ));
		memcpy( &file, private_data[1] + 0x1E90, 4 ); assert( file == key );
		memcpy( &file, private_data[0] + 0x420, 4 ); assert( !file );            /* the sender had it already */
		SV_CoF_StateMessage( clients[1].edict, "Objectives", (byte *)message, sizeof( message ) - 1 );
		memcpy( private_data[1] + 0x420, &none, 4 );                            /* the partner's PostThink */
		SV_CoF_StateFrame(); SV_CoF_StateFrame();
		memcpy( &file, private_data[1] + 0x420, 4 ); assert( !file );
		memcpy( &file, private_data[0] + 0x420, 4 ); assert( !file );
		/* a default hints file is no custom file */
		SV_CoF_StateMessage( clients[0].edict, "Objectives", (byte *)plain, sizeof( plain ) - 1 );
		SV_CoF_StateFrame(); memcpy( &file, private_data[1] + 0x420, 4 ); assert( !file );
	}
	/* The popup: cof_objective's showobjective is one global that the first PostThink sending the objective clears,
	 * so a partner sent the mirrored objective got it without the popup. The engine sets isNew for that player once. */
	{
		static const byte shown[] = "gotothepark\0txtfiles/hints.txt\0\1";
		byte quiet[] = "gotothepark\0txtfiles/hints.txt\0\0", other[] = "somethingelse\0txtfiles/hints.txt\0\0";
		const size_t at = sizeof( quiet ) - 2;
		SV_CoF_StateMessage( clients[0].edict, "Objectives", (byte *)shown, sizeof( shown ) - 1 );
		SV_CoF_StateMessage( clients[1].edict, "Objectives", quiet, sizeof( quiet ) - 1 ); assert( quiet[at] == 1 );
		quiet[at] = 0; SV_CoF_StateMessage( clients[1].edict, "Objectives", quiet, sizeof( quiet ) - 1 ); assert( quiet[at] == 0 ); /* once */
		quiet[at] = 0; SV_CoF_StateMessage( clients[0].edict, "Objectives", quiet, sizeof( quiet ) - 1 ); assert( quiet[at] == 0 ); /* the sender saw it */
		SV_CoF_StateMessage( clients[1].edict, "Objectives", other, sizeof( other ) - 1 ); assert( other[sizeof( other ) - 2] == 0 ); /* never popped up */
	}
	/* The story's control hints (cof_hint: c_start's tutorial) reach their trigger's activator only: the party gets each
	 * once, at the next frame. A weapon's own hints stay the player's. */
	{
		static const byte quick[] = "quickslots\0txtfiles/hints.txt", sights[] = "ironsights\0txtfiles/hints.txt"; /* both strings' ends sent */
		SV_CoF_StateMessage( clients[0].edict, "ControlHint", (byte *)quick, sizeof( quick ) );
		last_msg_name[0] = 0; SV_CoF_StateFrame();
		assert( !strcmp( last_msg_name, "ControlHint" ) && !strcmp( (const char *)last_msg, "quickslots" ) && last_msg_reliable );
		assert( !strcmp( (const char *)last_msg + 11, "txtfiles/hints.txt" ) && last_msg_size == (int)sizeof( quick ) );
		last_msg_name[0] = 0; SV_CoF_StateFrame(); assert( !last_msg_name[0] ); /* once */
		SV_CoF_StateMessage( clients[1].edict, "ControlHint", (byte *)quick, sizeof( quick ) ); /* the partner's own trigger later */
		last_msg_name[0] = 0; SV_CoF_StateFrame(); assert( !last_msg_name[0] ); /* both had it */
		SV_CoF_StateMessage( clients[0].edict, "ControlHint", (byte *)sights, sizeof( sights ) );
		last_msg_name[0] = 0; SV_CoF_StateFrame(); assert( !last_msg_name[0] ); /* the Glock's own */
	}
	/* Tape saves on the hardest difficulty: the cassette gives its five tapes to its picker only (CoF_KeyItem), and
	 * each save takes one from the saver: the party's tapes are one count. */
	{
		int tapes = 5;
		memcpy( private_data[1] + 0x1EF4, &tapes, 4 ); SV_CoF_StateFrame();
		memcpy( &tapes, private_data[0] + 0x1EF4, 4 ); assert( tapes == 5 );
		tapes = 4; memcpy( private_data[0] + 0x1EF4, &tapes, 4 ); SV_CoF_StateFrame();
		memcpy( &tapes, private_data[1] + 0x1EF4, 4 ); assert( tapes == 4 );
	}
	/* 1.1.9 (M2 F10, K): the health cap is the party's. The answerer's cof_maxhealthchange reaches the partner (with
	 * its HealthCap), a lift on one player (strangle, wheelchair) lifts everyone's, a rejoined player gets the party's;
	 * a new session's first look with a cap fills the party in (a checkpoint from before 1.1.9) */
	{
		int cap = 2;
		console_length = 0; console[0] = 0;
		SV_CoF_StateFrame();
		memcpy( private_data[1] + 0x60C, &cap, 4 ); last_msg_name[0] = 0;
		SV_CoF_StateFrame();
		memcpy( &cap, private_data[0] + 0x60C, 4 ); assert( cap == 2 && !strcmp( last_msg_name, "HealthCap" ) && last_msg[0] == 2 );
		assert( strstr( console, "cofstory phase=health-cap userid=10 cap=2" ) && strstr( console, "phase=health-cap-shared userid=1 cap=2" ));
		cap = 0; memcpy( private_data[0] + 0x60C, &cap, 4 ); SV_CoF_StateFrame();
		memcpy( &cap, private_data[1] + 0x60C, 4 ); assert( cap == 0 );
		cap = 1; memcpy( private_data[0] + 0x60C, &cap, 4 ); SV_CoF_StateFrame();
		clients[1].userid = 11; memset( private_data[1] + 0x60C, 0, 4 ); SV_CoF_StateFrame();
		memcpy( &cap, private_data[1] + 0x60C, 4 ); assert( cap == 1 );
		memcpy( &cap, private_data[0] + 0x60C, 4 ); assert( cap == 1 ); /* the newcomer's fresh 0 is no change */
		SV_CoF_StateReset(); memset( private_data[0] + 0x60C, 0, 4 ); cap = 2; memcpy( private_data[1] + 0x60C, &cap, 4 );
		SV_CoF_StateFrame(); memcpy( &cap, private_data[0] + 0x60C, 4 ); assert( cap == 2 );
		memset( private_data[0] + 0x60C, 0, 4 ); memset( private_data[1] + 0x60C, 0, 4 ); SV_CoF_StateReset(); SV_CoF_StateFrame();
	}
	/* 1.1.9 (M1 9): c_subway1st1's cof_phonedisable broke the toucher's phone alone. The party's phone breaks for
	 * everyone (the broken body too), again when a respawn's recovery hands back an inventory from before; a mend
	 * (the battery) ends it: the others mend their own */
	{
		int value, body = 0x2A;
		console_length = 0; console[0] = 0;
		for( value = 4; value < 12; value++ ) { edicts[value].free = 1; edicts[value].pvPrivateData = NULL; }
		svgame.numEntities = 12;
		weapon( 4, 0, SV_AllocString( "weapon_mobile" ), private_data[0], 2, -1, 0 );
		weapon( 5, 1, SV_AllocString( "weapon_mobile" ), private_data[1], 2, -1, 0 );
		SV_CoF_StateFrame();
		weapon_private[1][0x1F9] = 1; value = 1; memcpy( private_data[1] + 0x1F14, &value, 4 ); memcpy( private_data[1] + 0x5CC, &body, 4 );
		SV_CoF_StateFrame();
		memcpy( &value, private_data[0] + 0x1F14, 4 ); assert( value == 1 && weapon_private[0][0x1F9] == 1 );
		memcpy( &value, private_data[0] + 0x5CC, 4 ); assert( value == 0x2A );
		assert( strstr( console, "cofstory phase=phone-broken userid=11" ) && strstr( console, "cofstory phase=phone-shared userid=1\n" ));
		clients[0].edict->v.health = 0; SV_CoF_StateFrame();
		weapon_private[0][0x1F9] = 0; value = 0; memcpy( private_data[0] + 0x1F14, &value, 4 ); /* the respawn's restore */
		clients[0].edict->v.health = 100; SV_CoF_StateFrame();
		memcpy( &value, private_data[0] + 0x1F14, 4 ); assert( value == 1 && weapon_private[0][0x1F9] == 1 );
		assert( !strstr( console, "phone-mended" ));
		weapon_private[1][0x1F9] = 0; value = 0; memcpy( private_data[1] + 0x1F14, &value, 4 ); /* the partner's battery */
		SV_CoF_StateFrame(); SV_CoF_StateFrame();
		memcpy( &value, private_data[1] + 0x1F14, 4 ); assert( value == 0 && !weapon_private[1][0x1F9] );
		memcpy( &value, private_data[0] + 0x1F14, 4 ); assert( value == 1 && strstr( console, "cofstory phase=phone-mended userid=11" ));
		weapon_private[0][0x1F9] = 0; value = 0; memcpy( private_data[0] + 0x1F14, &value, 4 ); SV_CoF_StateFrame();
		memset( private_data[0] + 0x5CC, 0, 4 ); memset( private_data[1] + 0x5CC, 0, 4 );
	}
	/* 1.1.9 (K9): a player still in c_asylumb's bar puzzle once it is solved is let out by the state frame */
	{
		edict_t *button = &edicts[6], *camera = &edicts[7];
		memset( button, 0, sizeof( *button )); memset( camera, 0, sizeof( *camera ));
		button->v.classname = SV_AllocString( "cof_puzzlebarbutton" ); button->v.message = SV_AllocString( "deadfidafv" );
		button->v.iuser1 = 1; camera->v.targetname = SV_AllocString( "deadfidafv" );
		handle( private_data[1] + 0x378, button ); clients[1].pViewEntity = camera;
		console_length = 0; console[0] = 0;
		SV_CoF_StateFrame();
		assert( !clients[1].pViewEntity && strstr( console, "cofstory phase=puzzle-released userid=11" ));
		button->free = camera->free = 1;
	}
	gate = 0; observed.code = 5555; SV_CoF_RetailStoryWrite( &clients[1], &observed );
	SV_CoF_StateFrame();
	assert( SV_CoF_RetailStoryRead( &clients[0], &observed ) && observed.code == 9999 );
	puts( "state integration scenarios passed" );
	return 0;
}
