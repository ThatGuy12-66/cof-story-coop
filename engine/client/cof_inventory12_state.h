/* cof_inventory12_state.h - Cry of Fear story co-op: the client's copy of the
 * 12 inventory slots. GPL-3.0-or-later.
 *
 * Pure: no engine types; included by cof_inventory12.c and by the offline
 * harness. client.dll keeps char[128] per slot for slots 1..6 only, reached
 * through one accessor (1006CDD0), which the engine redirects here
 * (cof_inventory12_patch.h). Its Inventory handler copies the descriptor
 * before any range check, into read-only memory for slot 0 or 7+: those never
 * reach it. evidence/inventory-grid-20261013/client-inventory-re.md section 2.
 */
#ifndef COF_INVENTORY12_STATE_H
#define COF_INVENTORY12_STATE_H
#include <stdio.h>
#include <string.h>

#define COF_INV12_SLOTS 12
#define COF_INV12_TEXT 128

typedef struct
{
	char slot[COF_INV12_SLOTS + 1][COF_INV12_TEXT]; // 1..12; [0] is never used
	char scratch[COF_INV12_TEXT];                   // the accessor's answer for any other number: writable, never read
	int three;                                      // the story's three-slot sections (from the last Inventory message)
} cof_inv12_slots_t;

typedef struct
{
	int slot, reset, three, silent, tac;
	char descriptor[COF_INV12_TEXT];
} cof_inv12_message_t;

enum { COF_INV12_FORWARD = 0, COF_INV12_KEPT, COF_INV12_DROPPED };

/* Inventory (hl.dll 100E4360): byte slot, byte reset, string descriptor, byte
 * three, byte silent, byte tac glock. 0 when malformed. */
static int CoF_Inv12Parse( const unsigned char *buf, int size, cof_inv12_message_t *m )
{
	int i = 2, n = 0;
	memset( m, 0, sizeof( *m ));
	if( size < 6 ) return 0;
	m->slot = buf[0];
	m->reset = buf[1];
	for( ; i < size && buf[i]; i++ )
		if( n + 1 < COF_INV12_TEXT ) m->descriptor[n++] = (char)buf[i];
	if( i + 3 >= size ) return 0; // the terminator and three bytes after it
	m->three = buf[i + 1];
	m->silent = buf[i + 2];
	m->tac = buf[i + 3];
	return 1;
}

/* What client.dll's accessor returns for slot n. */
static char *CoF_Inv12Slot( cof_inv12_slots_t *s, int n )
{
	if( n >= 1 && n <= COF_INV12_SLOTS ) return s->slot[n];
	memset( s->scratch, 0, sizeof( s->scratch ));
	return s->scratch;
}

/* client.dll's tac-glock rename (it does slots 1..6 and the quick slots), for 7..12. */
static void CoF_Inv12Tac( cof_inv12_slots_t *s, int tac )
{
	const char *from = tac ? "weapon_glock.txt" : "weapon_glocktac.txt";
	const char *to = tac ? "weapon_glocktac.txt" : "weapon_glock.txt";
	size_t fl = strlen( from ), tl = strlen( to );
	int n;
	for( n = 7; n <= COF_INV12_SLOTS; n++ )
	{
		char *d = s->slot[n];
		size_t len = strlen( d );
		if( len < fl || strcmp( d + len - fl, from ) || len - fl + tl >= COF_INV12_TEXT ) continue;
		memcpy( d + len - fl, to, tl + 1 );
	}
}

/* 1..6 go on to client.dll, which writes them through the accessor; 7..12 are
 * kept here; anything else is dropped. The store follows every one it takes. */
static int CoF_Inv12Route( cof_inv12_slots_t *s, const cof_inv12_message_t *m )
{
	if( m->slot < 1 || m->slot > COF_INV12_SLOTS ) return COF_INV12_DROPPED;
	s->three = m->three;
	memcpy( s->slot[m->slot], m->descriptor, sizeof( m->descriptor ));
	CoF_Inv12Tac( s, m->tac );
	return m->slot <= 6 ? COF_INV12_FORWARD : COF_INV12_KEPT;
}

/* ResetHUD: client.dll clears 1..6 itself (through the accessor). */
static void CoF_Inv12ResetHigh( cof_inv12_slots_t *s )
{
	int n;
	for( n = 7; n <= COF_INV12_SLOTS; n++ ) s->slot[n][0] = 0;
}

/* The line client.dll shows for a pickup, as a ProFont payload: colour 5, then
 * "You got the X" ("You got Simon's Book"). Its length, or 0. */
static int CoF_Inv12PickupLine( const char *nice, unsigned char *out, int size )
{
	const char *format = !strcmp( nice, "Simon's Book" ) ? "You got %s" : "You got the %s";
	int n;
	if( !nice[0] || size < 3 ) return 0;
	out[0] = 5;
	n = snprintf( (char *)out + 1, (size_t)size - 1, format, nice );
	if( n < 0 || n >= size - 1 ) return 0;
	return n + 2;
}

#endif /* COF_INVENTORY12_STATE_H */
