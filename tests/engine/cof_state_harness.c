#define main retail_fixture_main
#include "retail_fixture.c"
#undef main
#define MAX_CLIENTS 32
#define FCL_FAKECLIENT 128
#define DEAD_NO 0
#define cs_connected 2
#define Q_stricmp _stricmp
static struct { int spawncount; } state_svs;
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
	/* Tape saves on the hardest difficulty: the cassette gives its five tapes to its picker only (CoF_KeyItem), and
	 * each save takes one from the saver: the party's tapes are one count. */
	{
		int tapes = 5;
		memcpy( private_data[1] + 0x1EF4, &tapes, 4 ); SV_CoF_StateFrame();
		memcpy( &tapes, private_data[0] + 0x1EF4, 4 ); assert( tapes == 5 );
		tapes = 4; memcpy( private_data[0] + 0x1EF4, &tapes, 4 ); SV_CoF_StateFrame();
		memcpy( &tapes, private_data[1] + 0x1EF4, 4 ); assert( tapes == 4 );
	}
	gate = 0; observed.code = 5555; SV_CoF_RetailStoryWrite( &clients[1], &observed );
	SV_CoF_StateFrame();
	assert( SV_CoF_RetailStoryRead( &clients[0], &observed ) && observed.code == 9999 );
	puts( "state integration scenarios passed" );
	return 0;
}
