/* 1.4: sv_cof_dynloops.h, the looping sounds counted per entity and the wav cue check, offline. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dynloops_under_test.h"
#include "dynloops_files.h" /* static const struct { int loops; const char *path; } wav_files[]; and WAV_FILES */

typedef struct { const unsigned char *data; int size, at; } buffer_t;

static int buffer_read( void *ctx, void *out, int len )
{
	buffer_t *b = ctx;
	int n = len < b->size - b->at ? len : b->size - b->at;
	if( n < 0 ) n = 0;
	memcpy( out, b->data + b->at, n );
	b->at += n;
	return n;
}

static int buffer_skip( void *ctx, int len )
{
	buffer_t *b = ctx;
	if( len < 0 || b->at + len > b->size ) return 0;
	b->at += len;
	return 1;
}

static int put_chunk( unsigned char *out, const char *id, unsigned int size )
{
	memcpy( out, id, 4 );
	out[4] = size & 0xFF; out[5] = ( size >> 8 ) & 0xFF; out[6] = ( size >> 16 ) & 0xFF; out[7] = size >> 24;
	memset( out + 8, 0x55, size + ( size & 1 ));
	return 8 + (int)( size + ( size & 1 ));
}

static int cue_of( const unsigned char *data, int size )
{
	buffer_t b = { data, size, 0 };
	return CoF_DynLoopWavCue( buffer_read, buffer_skip, &b );
}

static int test_file( const char *path )
{
	static unsigned char data[1 << 21];
	FILE *f = fopen( path, "rb" );
	int size;
	if( !f ) return -1;
	size = (int)fread( data, 1, sizeof( data ), f );
	fclose( f );
	return cue_of( data, size );
}

int main( void )
{
	static cof_dynloops_t l;
	unsigned char wav[512];
	int n, at, i;

	/* the wav check: a cue chunk anywhere in the chunk list (after the data, as chainsaw_loop.wav has it) */
	memcpy( wav, "RIFF\0\0\0\0WAVE", 12 ); n = 12;
	n += put_chunk( wav + n, "fmt ", 16 );
	n += put_chunk( wav + n, "data", 101 ); /* odd: padded to even, as the client's loader steps */
	assert( cue_of( wav, n ) == 0 );
	n += put_chunk( wav + n, "LIST", 6 );
	n += put_chunk( wav + n, "cue ", 28 );
	assert( cue_of( wav, n ) == 1 );
	memcpy( wav + 8, "AVI ", 4 ); assert( cue_of( wav, n ) == 0 ); memcpy( wav + 8, "WAVE", 4 ); /* not a wav */
	assert( cue_of( wav, 20 ) == 0 ); /* cut short */
	wav[12 + 4] = 0xFF; wav[12 + 5] = 0xFF; wav[12 + 6] = 0xFF; wav[12 + 7] = 0xFF; /* a chunk size past the file */
	assert( cue_of( wav, n ) == 0 );

	/* the real files, when the harness is given them: chainsaw_loop.wav loops, death.wav does not */
	for( i = 0; i < WAV_FILES; i++ )
	{
		int got = test_file( wav_files[i].path );
		if( got < 0 ) continue;
		assert( got == wav_files[i].loops );
		printf( "file %s cue=%d\n", wav_files[i].path, got );
	}

	/* the count: each start of the same looping sample is one more copy; another entity, channel, sample or serial
	 * is another entry */
	memset( &l, 0, sizeof( l ));
	assert( CoF_DynLoopStarted( &l, 40, 7, 0, 12 ) == 1 );
	assert( CoF_DynLoopStarted( &l, 40, 7, 0, 12 ) == 2 );
	assert( CoF_DynLoopStarted( &l, 40, 7, 0, 12 ) == 3 );
	assert( CoF_DynLoopStarted( &l, 40, 8, 0, 12 ) == 1 ); /* the edict freed and given to another entity */
	assert( CoF_DynLoopStarted( &l, 40, 7, 4, 12 ) == 1 && CoF_DynLoopStarted( &l, 41, 7, 0, 12 ) == 1 &&
		CoF_DynLoopStarted( &l, 40, 7, 0, 13 ) == 1 && l.count == 5 );
	/* the game's stop: the copies it had, so the stop goes out once for each; then nothing left */
	assert( CoF_DynLoopStopped( &l, 40, 7, 0, 12 ) == 3 && CoF_DynLoopStopped( &l, 40, 7, 0, 12 ) == 0 && l.count == 4 );
	/* a freed entity: each of its entries (and only its), whatever the order */
	at = 0; n = 0;
	while(( at = CoF_DynLoopOfEntity( &l, 40, 7, at )) >= 0 ) { n += l.loops[at].copies; CoF_DynLoopRemove( &l, at ); }
	assert( n == 2 && l.count == 2 && CoF_DynLoopFind( &l, 40, 8, 0, 12 ) >= 0 && CoF_DynLoopFind( &l, 41, 7, 0, 12 ) >= 0 );
	/* the copies are capped, and a full list takes no more */
	for( i = 0; i < 100; i++ ) CoF_DynLoopStarted( &l, 41, 7, 0, 12 );
	assert( l.loops[CoF_DynLoopFind( &l, 41, 7, 0, 12 )].copies == COF_DYNLOOPS_COPIES );
	for( i = 0; l.count < COF_DYNLOOPS_MAX; i++ ) assert( CoF_DynLoopStarted( &l, 100 + i, 1, 0, 1 ) == 1 );
	assert( CoF_DynLoopStarted( &l, 9999, 1, 0, 1 ) == 0 && l.count == COF_DYNLOOPS_MAX );

	puts( "dynloops scenarios passed" );
	return 0;
}
