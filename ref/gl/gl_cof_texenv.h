/*
gl_cof_texenv.h - Cry of Fear: the texture environment client.dll's renderer draws with
GPL-3.0-or-later.

Cry of Fear's client.dll draws the world and its solid brush entities itself (a
Paranoia renderer). Its texture-unit modes set the GL_COMBINE colour only and
rely on GL's initial alpha combine, alpha from the texture: that is how the
holes of a "{" texture reach the alpha test of a solid (rendermode 4) brush
entity. GoldSrc never changes those states. This engine's lightmap pass
(gl_rsurf.c R_SetLightmap, gl_overbright 1) left the lightmap unit's alpha at
REPLACE from PREVIOUS, and the client then drew every hole of such an entity:
the {invisible use-boxes round c_village1's cars as black boxes, the house doors
black (2026-10-17; gl_overbright 0 at runtime took them away). The client draws
them outside its triangle callbacks (with both off the boxes stayed), so the
engine's VBO pass itself ends with every unit at GL's initial environment
(R_ClearVBOState, cof_client_texenv).
*/
#ifndef GL_COF_TEXENV_H
#define GL_COF_TEXENV_H

typedef struct
{
	void (*select)( int unit );
	void (*texenvi)( int pname, int value );
	void (*texenvfv)( int pname, const float *value );
} cof_texenv_api_t;

// GL 1.3 / ARB_texture_env_combine initial values of a texture unit's environment
static const int cof_texenv_initial[][2] =
{
	{ GL_TEXTURE_ENV_MODE, GL_MODULATE },
	{ GL_COMBINE_RGB_ARB, GL_MODULATE },
	{ GL_COMBINE_ALPHA_ARB, GL_MODULATE },
	{ GL_SOURCE0_RGB_ARB, GL_TEXTURE },
	{ GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB },
	{ GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB },
	{ GL_SOURCE0_ALPHA_ARB, GL_TEXTURE },
	{ GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB },
	{ GL_SOURCE2_ALPHA_ARB, GL_CONSTANT_ARB },
	{ GL_OPERAND0_RGB_ARB, GL_SRC_COLOR },
	{ GL_OPERAND1_RGB_ARB, GL_SRC_COLOR },
	{ GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA },
	{ GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA },
	{ GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA },
	{ GL_OPERAND2_ALPHA_ARB, GL_SRC_ALPHA },
	{ GL_RGB_SCALE_ARB, 1 },
	{ GL_ALPHA_SCALE, 1 },
};

// every unit from the top down to 0, which is left selected
static void CoF_TexEnvDefaults( const cof_texenv_api_t *gl, int units )
{
	static const float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	for( int unit = units - 1; unit >= 0; unit-- )
	{
		gl->select( unit );
		for( size_t i = 0; i < sizeof( cof_texenv_initial ) / sizeof( cof_texenv_initial[0] ); i++ )
			gl->texenvi( cof_texenv_initial[i][0], cof_texenv_initial[i][1] );
		gl->texenvfv( GL_TEXTURE_ENV_COLOR, color );
	}
}

#endif // GL_COF_TEXENV_H
