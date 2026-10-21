/* The frame of the model a guest's view rides (engine/client/cl_frame.c CL_CoF_AttachCamEstimate): a ladder's hands
 * between packets. No renderer, model, network or game. */
#include <stdio.h>
#include <math.h>

typedef int qboolean;
#define true 1
#define false 0
#define bound( min, num, max ) (( num ) >= ( min ) ? (( num ) < ( max ) ? ( num ) : ( max )) : ( min ))

#include "attachcam_under_test.c"

#define CHECK( expr ) do { if( !( expr )) { printf( "FAIL line %d: %s\n", __LINE__, #expr ); return 1; } } while( 0 )
#define FPS 30.0f /* a climb step: 31 frames at 30 a second (256 units a second), framerate 1 */
#define FRAMES 31

/* The server's frame of a step started at `start`, at time t (non-looping: held at 255 at its end). */
static float truth( double t, double start )
{
	double f = ( t - start ) * FPS * 256.0 / ( FRAMES - 1 );
	return (float)( f > 255.0 ? 255.0 : f < 0.0 ? 0.0 : f );
}

int main( void )
{
	cl_cof_attachcam_t last = { -1, 0.0f, 0.0f };
	float shown, before;
	double t;
	int k;

	/* A first packet: its own frame (it may be a little ahead of the client's time); then on at the sequence's rate. */
	CHECK( CL_CoF_AttachCamEstimate( &last, 3, 100.0f, -0.01, FPS, FRAMES, false ) == 100.0f );
	shown = CL_CoF_AttachCamEstimate( &last, 3, 100.0f, 0.01, FPS, FRAMES, false );
	CHECK( fabs( shown - 102.56f ) < 0.01f );
	/* the next packet a little behind the frame shown: held, never back */
	CHECK( CL_CoF_AttachCamEstimate( &last, 3, 102.0f, 0.0, FPS, FRAMES, false ) == shown );
	CHECK( CL_CoF_AttachCamEstimate( &last, 3, 102.0f, 0.01, FPS, FRAMES, false ) > shown );
	/* at most CL_COF_ATTACHCAM_AHEAD past the packet (a late packet), and never past the end of a step */
	CHECK( fabs( CL_CoF_AttachCamEstimate( &last, 3, 102.0f, 1.0, FPS, FRAMES, false ) - ( 102.0f + 256.0f * (float)CL_COF_ATTACHCAM_AHEAD )) < 0.01f );
	CHECK( CL_CoF_AttachCamEstimate( &last, 3, 250.0f, 0.05, FPS, FRAMES, false ) == 255.0f );
	/* the same step started again (the next rung: frame 0) is not held at the end */
	CHECK( CL_CoF_AttachCamEstimate( &last, 3, 0.0f, 0.0, FPS, FRAMES, false ) == 0.0f );
	/* another sequence starts at its packet's frame, even with the client's time a little behind it */
	CHECK( CL_CoF_AttachCamEstimate( &last, 4, 0.0f, -0.02, FPS, FRAMES, false ) == 0.0f );
	/* a looping one goes round, forward */
	CHECK( fabs( CL_CoF_AttachCamEstimate( &last, 5, 250.0f, 0.04, FPS, FRAMES, true ) - 4.24f ) < 0.01f );
	CHECK( fabs( CL_CoF_AttachCamEstimate( &last, 5, 2.0f, 0.0, FPS, FRAMES, true ) - 4.24f ) < 0.01f ); /* behind it: held */
	CHECK( CL_CoF_AttachCamEstimate( &last, 5, 6.0f, 0.0, FPS, FRAMES, true ) == 6.0f );
	/* nothing to move: one frame, or no rate */
	CHECK( CL_CoF_AttachCamEstimate( &last, 6, 40.0f, 0.05, FPS, 1, false ) == 40.0f );
	CHECK( CL_CoF_AttachCamEstimate( &last, 7, 40.0f, 0.05, 0.0f, FRAMES, false ) == 40.0f );

	/* A climb step seen by a guest: packets every 1/60 s with 10 ms of jitter, animtime in hundredths, frame in
	 * 256ths; drawn 144 times a second at the client's time. As sent it stepped by up to a packet's worth and more;
	 * now it follows the step to about a frame and never goes back. */
	{
		double start = 10.0, arrive[64], at[64];
		float frame[64], worst_now = 0.0f, worst_old = 0.0f, biggest_step = 0.0f;
		unsigned int seed = 7u;
		int have = -1;
		last.sequence = -1;
		for( k = 0; k < 64; k++ )
		{
			double sent = start + k / 60.0;
			seed = seed * 1103515245u + 12345u;
			at[k] = (double)(long)( sent * 100.0 + 0.5 ) / 100.0; /* hundredths, as DT_TIMEWINDOW_8 */
			frame[k] = (float)(int)truth( sent, start ); /* 256ths: the 8-bit field */
			arrive[k] = sent + (( seed >> 8 ) & 0xFF ) / 255.0 * 0.01;
		}
		before = 0.0f;
		for( t = start; t < start + 1.0; t += 1.0 / 144.0 )
		{
			while( have < 63 && arrive[have + 1] <= t ) have++;
			if( have < 0 ) continue;
			shown = CL_CoF_AttachCamEstimate( &last, 3, frame[have], t - at[have], FPS, FRAMES, false );
			CHECK( shown >= before );
			if( shown - before > biggest_step && t > start + 0.05 ) biggest_step = shown - before;
			before = shown;
			if( fabs( shown - truth( t, start )) > worst_now ) worst_now = (float)fabs( shown - truth( t, start ));
			if( fabs( frame[have] - truth( t, start )) > worst_old ) worst_old = (float)fabs( frame[have] - truth( t, start ));
		}
		printf( "a climb step: off by %.1f 256ths as sent, %.1f now; largest step %.1f (%.1f a frame at 144 fps)\n",
			worst_old, worst_now, biggest_step, 256.0 / 144.0 );
		CHECK( worst_old > 5.0f && worst_now < 4.0f && worst_now < worst_old / 2.0f && biggest_step < 6.0f );
	}
	puts( "attach camera frame passed" );
	return 0;
}
