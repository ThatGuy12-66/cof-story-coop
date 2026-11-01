/* Cry of Fear co-op 1.4: looping sounds on a dynamic channel, stopped with their entity (sv_cof_dynloops.c).
 * GPL-3.0-or-later.
 *
 * A player's report (2026-10-31, the story, 1.3.8): after the forest's chainsaw encounter "The idle chainsaw noise
 * ... replaced the idling sound of normal enemies, their death sound too, and since that sound is on loop it kept
 * playing", until the game was restarted. The Sawrunner's AlertSound (and the Sawer boss's) starts
 * boss/sawer/chainsaw_loop.wav, a wav with a cue chunk (it loops until stopped), on CHAN_AUTO: a new channel at every
 * alert, and in co-op the monster is alerted again each time it takes another player or companion for its enemy.
 * Its death stops it once (STOP_SOUND, one SND_STOP), and the client stops only the first copy it finds
 * (S_AlterChannel); c_forest3 removes its Sawrunner with a killtarget (trigger_relay "killsawrunner"), which stops
 * none. A client plays a sound at its entity's number: the copies left kept looping at that number, and every monster
 * given the freed edict afterwards (a monstermaker's) seemed to make the chainsaw noise.
 *
 * The server keeps, per entity, how many times each looping sample was started on a dynamic channel: a stop the game
 * sends is repeated for every copy (an older client stops one per message), and the copies of an entity that is freed
 * are stopped. A sample loops when its wav has a cue chunk, as the client's loader decides (snd_wav.c).
 *
 * Pure: no engine headers; the offline harness compiles it as it is. */
#ifndef SV_COF_DYNLOOPS_H
#define SV_COF_DYNLOOPS_H
#include <stddef.h>
#include <string.h>

#define COF_DYNLOOPS_MAX    256 // entities' looping samples at once: a map has a handful
#define COF_DYNLOOPS_COPIES 32  // copies counted per entry (and stops sent for them): more than a client's channels

typedef struct
{
	int entnum, serial; // the entity (its serial: a freed and reused edict is another entity)
	int chan, sound;    // its dynamic channel and the sample's precache index
	int copies;         // started and not stopped
} cof_dynloop_t;

typedef struct
{
	int count;
	cof_dynloop_t loops[COF_DYNLOOPS_MAX];
} cof_dynloops_t;

static int CoF_DynLoopFind( const cof_dynloops_t *l, int entnum, int serial, int chan, int sound )
{
	int i;
	for( i = 0; i < l->count; i++ )
	{
		const cof_dynloop_t *e = &l->loops[i];
		if( e->entnum == entnum && e->serial == serial && e->chan == chan && e->sound == sound ) return i;
	}
	return -1;
}

static void CoF_DynLoopRemove( cof_dynloops_t *l, int at )
{
	if( at < 0 || at >= l->count ) return;
	l->loops[at] = l->loops[--l->count];
}

// One more copy started: its entry's copies (1 for a new one), or 0 when the list is full.
static int CoF_DynLoopStarted( cof_dynloops_t *l, int entnum, int serial, int chan, int sound )
{
	int at = CoF_DynLoopFind( l, entnum, serial, chan, sound );
	cof_dynloop_t *e;
	if( at >= 0 )
	{
		e = &l->loops[at];
		if( e->copies < COF_DYNLOOPS_COPIES ) e->copies++;
		return e->copies;
	}
	if( l->count >= COF_DYNLOOPS_MAX ) return 0;
	e = &l->loops[l->count++];
	e->entnum = entnum; e->serial = serial; e->chan = chan; e->sound = sound; e->copies = 1;
	return 1;
}

// The game stopped it: the copies it had (0: none recorded), and the entry is gone.
static int CoF_DynLoopStopped( cof_dynloops_t *l, int entnum, int serial, int chan, int sound )
{
	int at = CoF_DynLoopFind( l, entnum, serial, chan, sound ), copies;
	if( at < 0 ) return 0;
	copies = l->loops[at].copies;
	CoF_DynLoopRemove( l, at );
	return copies;
}

// The next entry of this entity (from *at on), or -1; the caller stops its copies and removes it.
static int CoF_DynLoopOfEntity( const cof_dynloops_t *l, int entnum, int serial, int at )
{
	for( ; at >= 0 && at < l->count; at++ )
		if( l->loops[at].entnum == entnum && l->loops[at].serial == serial ) return at;
	return -1;
}

/* A wav's chunks, as the client's loader walks them (FindChunk from the start of the RIFF body, sizes padded to
 * even): 1 when one is "cue " (the sound loops), 0 otherwise or not a wav. read( ctx, buf, n ) returns the bytes
 * read; skip( ctx, n ) returns nonzero when it moved n bytes on. */
#define COF_DYNLOOPS_CHUNKS 64
static int CoF_DynLoopWavCue( int ( *read )( void *ctx, void *buf, int len ), int ( *skip )( void *ctx, int len ), void *ctx )
{
	unsigned char head[12], chunk[8];
	int i;
	if( read( ctx, head, 12 ) != 12 || memcmp( head, "RIFF", 4 ) || memcmp( head + 8, "WAVE", 4 )) return 0;
	for( i = 0; i < COF_DYNLOOPS_CHUNKS; i++ )
	{
		unsigned int size;
		if( read( ctx, chunk, 8 ) != 8 ) return 0;
		if( !memcmp( chunk, "cue ", 4 )) return 1;
		size = (unsigned int)chunk[4] | (unsigned int)chunk[5] << 8 | (unsigned int)chunk[6] << 16 | (unsigned int)chunk[7] << 24;
		if( size > 0x7FFFFFFEu ) return 0;
		size = ( size + 1 ) & ~1u;
		if( size && !skip( ctx, (int)size )) return 0;
	}
	return 0;
}

#endif // SV_COF_DYNLOOPS_H
