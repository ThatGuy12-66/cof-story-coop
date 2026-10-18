/* gl_cof_texenv.h: every texture unit's environment back to GL's initial one before client.dll's renderer draws.
   A model of GL's per-unit texture environment, started from what the engine's overbright lightmap pass leaves
   (gl_rsurf.c R_SetLightmap: combine, alpha REPLACE from PREVIOUS, RGB scale 2), printed after the reset. */
#include <stdio.h>
#include <string.h>

#define GL_TEXTURE				0x1702
#define GL_SRC_COLOR			0x0300
#define GL_SRC_ALPHA			0x0302
#define GL_REPLACE				0x1E01
#define GL_TEXTURE_ENV_MODE		0x2200
#define GL_TEXTURE_ENV_COLOR	0x2201
#define GL_MODULATE				0x2100
#define GL_ALPHA_SCALE			0x0D1C
#define GL_COMBINE_ARB			0x8570
#define GL_COMBINE_RGB_ARB		0x8571
#define GL_COMBINE_ALPHA_ARB	0x8572
#define GL_SOURCE0_RGB_ARB		0x8580
#define GL_SOURCE1_RGB_ARB		0x8581
#define GL_SOURCE2_RGB_ARB		0x8582
#define GL_SOURCE0_ALPHA_ARB	0x8588
#define GL_SOURCE1_ALPHA_ARB	0x8589
#define GL_SOURCE2_ALPHA_ARB	0x858A
#define GL_OPERAND0_RGB_ARB		0x8590
#define GL_OPERAND1_RGB_ARB		0x8591
#define GL_OPERAND2_RGB_ARB		0x8592
#define GL_OPERAND0_ALPHA_ARB	0x8598
#define GL_OPERAND1_ALPHA_ARB	0x8599
#define GL_OPERAND2_ALPHA_ARB	0x859A
#define GL_RGB_SCALE_ARB		0x8573
#define GL_CONSTANT_ARB			0x8576
#define GL_PREVIOUS_ARB			0x8578
#include "texenv_under_test.h"

#define UNITS 4
static const struct { int pname; const char *name; } states[] =
{
	{ GL_TEXTURE_ENV_MODE, "TEXTURE_ENV_MODE" }, { GL_COMBINE_RGB_ARB, "COMBINE_RGB" },
	{ GL_COMBINE_ALPHA_ARB, "COMBINE_ALPHA" }, { GL_SOURCE0_RGB_ARB, "SOURCE0_RGB" },
	{ GL_SOURCE1_RGB_ARB, "SOURCE1_RGB" }, { GL_SOURCE2_RGB_ARB, "SOURCE2_RGB" },
	{ GL_SOURCE0_ALPHA_ARB, "SOURCE0_ALPHA" }, { GL_SOURCE1_ALPHA_ARB, "SOURCE1_ALPHA" },
	{ GL_SOURCE2_ALPHA_ARB, "SOURCE2_ALPHA" }, { GL_OPERAND0_RGB_ARB, "OPERAND0_RGB" },
	{ GL_OPERAND1_RGB_ARB, "OPERAND1_RGB" }, { GL_OPERAND2_RGB_ARB, "OPERAND2_RGB" },
	{ GL_OPERAND0_ALPHA_ARB, "OPERAND0_ALPHA" }, { GL_OPERAND1_ALPHA_ARB, "OPERAND1_ALPHA" },
	{ GL_OPERAND2_ALPHA_ARB, "OPERAND2_ALPHA" }, { GL_RGB_SCALE_ARB, "RGB_SCALE" }, { GL_ALPHA_SCALE, "ALPHA_SCALE" },
};
#define NSTATES ( sizeof( states ) / sizeof( states[0] ))
static int env[UNITS][NSTATES];
static float color[UNITS][4];
static int active;

static int slot( int pname )
{
	for( size_t i = 0; i < NSTATES; i++ )
		if( states[i].pname == pname ) return (int)i;
	printf( "unknown pname 0x%x\n", pname );
	return -1;
}

static void select_unit( int unit ) { active = unit; }
static void texenvi( int pname, int value ) { int k = slot( pname ); if( k >= 0 ) env[active][k] = value; }
static void texenvfv( int pname, const float *value )
{
	if( pname == GL_TEXTURE_ENV_COLOR ) memcpy( color[active], value, sizeof( color[active] ));
	else printf( "unknown float pname 0x%x\n", pname );
}

static const char *value_name( int v )
{
	static char number[16];
	switch( v )
	{
	case GL_MODULATE: return "MODULATE";
	case GL_REPLACE: return "REPLACE";
	case GL_COMBINE_ARB: return "COMBINE";
	case GL_TEXTURE: return "TEXTURE";
	case GL_PREVIOUS_ARB: return "PREVIOUS";
	case GL_CONSTANT_ARB: return "CONSTANT";
	case GL_SRC_COLOR: return "SRC_COLOR";
	case GL_SRC_ALPHA: return "SRC_ALPHA";
	}
	snprintf( number, sizeof( number ), "%d", v );
	return number;
}

int main( void )
{
	const cof_texenv_api_t api = { select_unit, texenvi, texenvfv };

	// the engine's leftovers: every unit at a non-initial value, the lightmap unit as R_SetLightmap leaves it
	for( int u = 0; u < UNITS; u++ )
	{
		for( size_t k = 0; k < NSTATES; k++ ) env[u][k] = -1;
		color[u][0] = color[u][1] = color[u][2] = 128.0f / 192.0f; color[u][3] = 1.0f;
	}
	env[1][slot( GL_TEXTURE_ENV_MODE )] = GL_COMBINE_ARB;
	env[1][slot( GL_COMBINE_ALPHA_ARB )] = GL_REPLACE;
	env[1][slot( GL_SOURCE0_ALPHA_ARB )] = GL_PREVIOUS_ARB;
	env[1][slot( GL_RGB_SCALE_ARB )] = 2;
	active = 2;

	CoF_TexEnvDefaults( &api, UNITS );

	for( int u = 0; u < UNITS; u++ )
	{
		printf( "unit %d:", u );
		for( size_t k = 0; k < NSTATES; k++ )
			printf( " %s=%s", states[k].name, value_name( env[u][k] ));
		printf( " TEXTURE_ENV_COLOR=%g,%g,%g,%g\n", color[u][0], color[u][1], color[u][2], color[u][3] );
	}
	printf( "active %d\n", active );
	return 0;
}
