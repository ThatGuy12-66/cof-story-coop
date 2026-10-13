#ifndef SV_COF_STATE_DATA_H
#define SV_COF_STATE_DATA_H
#include <string.h>

#define COF_STATE_NOTES 16
#define COF_STATE_MESSAGE 192
#define COF_STATE_TEXT 128
#define COF_STATE_BEGIN 16 // cof_begingame's stats block, P+0x678..0x687

typedef struct
{
	int code, code_type, digits[4];
	int ending[3], three_slots;
	char objective[COF_STATE_TEXT], objective_file[COF_STATE_TEXT];
	// m_iDoctorWeapon (P+0x5B0): 1 the P345 was given, 2 the revolver. Written to
	// "the first player" by cof_doctorweaponset, read from the first player by the
	// c_attic doctor (1002D800), who never fights while it is 0.
	int doctor_weapon;
	// cof_begingame (100A4080) marks the players present in c_nightmare; credits
	// show no ending for a player without it (cof_credits 100A2710).
	unsigned char begin[COF_STATE_BEGIN];
} cof_story_values_t;

typedef struct
{
	int size;
	unsigned char message[COF_STATE_MESSAGE];
} cof_story_note_t;

typedef struct
{
	int known, digits_known, note_count;
	cof_story_values_t values;
	cof_story_note_t notes[COF_STATE_NOTES];
} cof_state_data_t;

static void CoF_StateObserve( cof_state_data_t *party, const cof_story_values_t *current, const cof_story_values_t *previous )
{
	int index, valid_digits = 1;
	if( !party->known || ( previous && ( current->code != previous->code || current->code_type != previous->code_type )))
	{
		party->values.code = current->code;
		party->values.code_type = current->code_type;
	}
	if( !party->known || ( previous && ( strcmp( current->objective, previous->objective ) || strcmp( current->objective_file, previous->objective_file ))))
	{
		memcpy( party->values.objective, current->objective, sizeof( current->objective ));
		memcpy( party->values.objective_file, current->objective_file, sizeof( current->objective_file ));
	}
	for( index = 0; index < 4; index++ )
		if( current->digits[index] < 1 || current->digits[index] > 9 ) valid_digits = 0;
	if( !party->digits_known && valid_digits )
	{
		memcpy( party->values.digits, current->digits, sizeof( current->digits ));
		party->digits_known = 1;
	}
	for( index = 0; index < 3; index++ ) party->values.ending[index] |= !!current->ending[index];
	party->values.three_slots |= !!current->three_slots;
	// A choice made (1 or 2) is the party's. A player seen fresh (join, rejoin)
	// only fills in a party that has none; only a real change replaces it.
	if(( current->doctor_weapon == 1 || current->doctor_weapon == 2 ) &&
		( previous ? current->doctor_weapon != previous->doctor_weapon : !party->values.doctor_weapon ))
		party->values.doctor_weapon = current->doctor_weapon;
	if( current->begin[0] == 1 && party->values.begin[0] != 1 )
		memcpy( party->values.begin, current->begin, sizeof( party->values.begin ));
	party->known = 1;
}

static int CoF_StateNoteValid( const unsigned char *message, int size )
{
	static const int limits[11] = { 16, 16, 16, 16, 16, 16, 16, 16, 48, 64, 48 };
	int position = 5, field, length;
	if( !message || size < 16 || size > COF_STATE_MESSAGE || message[0] < 1 || message[0] > 16 || message[1] > 1 ) return 0;
	for( field = 0; field < 11; field++ )
	{
		length = 0;
		while( position < size && message[position] ) { position++; length++; }
		if( position >= size || length >= limits[field] ) return 0;
		position++;
	}
	return position == size && message[5] != 0;
}

static int CoF_StateNoteSame( const unsigned char *left, const unsigned char *right )
{
	if( left[1] != right[1] ) return 0;
	if( left[1] ) return !memcmp( left + 2, right + 2, 3 );
	return !strcmp( (const char *)left + 5, (const char *)right + 5 );
}

static int CoF_StateNote( cof_state_data_t *party, const unsigned char *message, int size )
{
	int index;
	if( !CoF_StateNoteValid( message, size )) return -1;
	if( !message[1] && !strcmp( (const char *)message + 5, "nerds" )) return -1;
	for( index = 0; index < party->note_count; index++ )
		if( CoF_StateNoteSame( party->notes[index].message, message )) return index;
	if( party->note_count == COF_STATE_NOTES ) return -1;
	party->notes[index].size = size;
	memcpy( party->notes[index].message, message, size );
	party->note_count++;
	return index;
}
#endif
