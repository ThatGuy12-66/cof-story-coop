/*
sv_cof_loops.h - Cry of Fear story co-op: a merged map's ambient loops, per section (sv_cof_loops.c)
GPL-3.0-or-later.

A looping ambient_generic (no spawnflag 32) plays from itself through pfnEmitAmbientSound: a start, changes of
volume or pitch while it ramps, a stop (sound.cpp CAmbientGeneric, docs/research/2026-10-18-cof-decompilation-
survey.md, the launcher repo). The engine sends every one to every player, so in a merged map each section heard the
others'. hl.dll starts every active one again when a level is restored (Precache), so a section's loops start for a
player entering it and stop for one leaving it, as across the original level changes.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_LOOPS_H
#define SV_COF_LOOPS_H

#if defined( _MSC_VER )
#define COF_LOOP_FN static __inline
#else
#define COF_LOOP_FN static inline
#endif

#define COF_LOOP_MAX 256
#define COF_LOOP_SAMPLE 64
#define COF_LOOP_UNSEEN -2 // a player's section before their first frame in the map

// what a player's move needs of one loop
#define COF_LOOP_KEEP 0
#define COF_LOOP_SEND_STOP 1
#define COF_LOOP_SEND_START 2

typedef struct
{
	int entnum, serial; // the ambient_generic's edict
	int section; // of the merged map, at its origin
	char sample[COF_LOOP_SAMPLE];
	float pos[3];
	int vol, pitch; // 0-255
	float attn;
} cof_loop_t;

typedef struct
{
	int count;
	cof_loop_t loop[COF_LOOP_MAX];
} cof_loops_t;

// The recorded loop of this entity and sound, or -1.
COF_LOOP_FN int CoF_LoopFind( const cof_loops_t *t, int entnum, const char *sample )
{
	int i;
	for( i = 0; i < t->count; i++ )
		if( t->loop[i].entnum == entnum && !strcmp( t->loop[i].sample, sample )) return i;
	return -1;
}

// A start, recorded in place of the same entity's same sound. 0 when it is not recorded (no sample, one too long
// or the table full): the engine sends it as it always did.
COF_LOOP_FN int CoF_LoopStart( cof_loops_t *t, const cof_loop_t *loop )
{
	int i;
	if( !loop->sample[0] || !memchr( loop->sample, 0, COF_LOOP_SAMPLE )) return 0;
	if(( i = CoF_LoopFind( t, loop->entnum, loop->sample )) < 0 )
	{
		if( t->count >= COF_LOOP_MAX ) return 0;
		i = t->count++;
	}
	t->loop[i] = *loop;
	return 1;
}

// A stop: the loop is forgotten.
COF_LOOP_FN void CoF_LoopStop( cof_loops_t *t, int entnum, const char *sample )
{
	int i = CoF_LoopFind( t, entnum, sample );
	if( i < 0 ) return;
	memmove( &t->loop[i], &t->loop[i + 1], ( t->count - i - 1 ) * sizeof( t->loop[0] ));
	t->count--;
}

// A ramp's change of volume or pitch (-1: unchanged). 0 when the loop is not recorded.
COF_LOOP_FN int CoF_LoopChange( cof_loops_t *t, int entnum, const char *sample, int vol, int pitch )
{
	int i = CoF_LoopFind( t, entnum, sample );
	if( i < 0 ) return 0;
	if( vol >= 0 ) t->loop[i].vol = vol;
	if( pitch >= 0 ) t->loop[i].pitch = pitch;
	return 1;
}

// What a player moving from section `from` (COF_LOOP_UNSEEN: their first frame in the map) to `to` needs of a loop:
// a stop when it is the section left's, a start when it is the new section's and its entity lives on (a removed
// ambient's sound played on until the level changed).
COF_LOOP_FN int CoF_LoopMove( const cof_loop_t *loop, int live, int from, int to )
{
	if( from == to ) return COF_LOOP_KEEP;
	if( loop->section == from ) return COF_LOOP_SEND_STOP;
	if( loop->section == to && live ) return COF_LOOP_SEND_START;
	return COF_LOOP_KEEP;
}

#endif // SV_COF_LOOPS_H
