/* Offline doubles for the gamepad defaults check (engine/client/input/cof_gamepad.c, 1.2.3: the engine's Half-Life
 * pad binds a profile kept move to the Cry of Fear layout). */
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "keydefs.h"
typedef int qboolean;
#define true 1
#define false 0
#define ARRAYSIZE( x ) ((int)( sizeof( x ) / sizeof(( x )[0] )))
#define Q_snprintf snprintf
#define Q_strlen strlen
typedef struct { float value; } convar_t;
static const char *bindings[256];
static const char *Key_GetBinding( int key ) { return bindings[key]; }
static void Key_SetBinding( int key, const char *binding ) { bindings[key] = binding; }
static int Q_stricmp( const char *a, const char *b )
{
	for( ; *a && tolower( (unsigned char)*a ) == tolower( (unsigned char)*b ); a++, b++ );
	return tolower( (unsigned char)*a ) - tolower( (unsigned char)*b );
}
static int COM_StringEmptyOrNULL( const char *s ) { return !s || !*s; }
static char printed[512];
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	vsnprintf( printed, sizeof( printed ), fmt, args );
	va_end( args );
}
static convar_t cof_pad_defaults_gen;
static void Cvar_DirectSetValue( convar_t *var, float value ) { var->value = value; }
static struct { char defaults_note[192]; } cof_pad;

#include "pad_under_test.c"

static int is( int key, const char *binding )
{
	const char *b = bindings[key] ? bindings[key] : "";
	return !strcmp( b, binding );
}

static int the_layout( void )
{
	int i;
	for( i = 0; i < (int)ARRAYSIZE( cof_pad_layout ); i++ )
		if( !is( cof_pad_layout[i].key, cof_pad_layout[i].binding )) return 0;
	return is( K_L2_BUTTON, "" ) && is( K_R2_BUTTON, "" );
}

static void profile( int gen, const cof_pad_bind_t *binds, int count )
{
	int i;
	for( i = 0; i < (int)ARRAYSIZE( bindings ); i++ ) bindings[i] = NULL;
	for( i = 0; i < count; i++ ) bindings[binds[i].key] = binds[i].binding;
	cof_pad_defaults_gen.value = (float)gen;
	cof_pad.defaults_note[0] = printed[0] = 0;
}

int main( void )
{
	/* the guest's config.cfg (2026-10-21): generation 5, every pad key on the engine's Half-Life default */
	profile( 5, cof_pad_layout_builtin, ARRAYSIZE( cof_pad_layout_builtin ));
	CL_CoF_PadDefaultsCheck();
	assert( the_layout() && cof_pad_defaults_gen.value == COF_PAD_DEFAULTS_GENERATION );
	assert( is( K_JOY1, "+attack3" ) && is( K_LSTICK, "cof_sprint_toggle_press" ) && is( K_DPAD_DOWN, "weapontoggle" ) && is( K_BACK_BUTTON, "+inventory" ));
	assert( strstr( printed, "15 of the engine's Half-Life pad binds moved to the Cry of Fear layout (generation 6)" ));
	/* ...once: the next launch leaves the keys alone */
	bindings[K_LSTICK] = "+speed";
	CL_CoF_PadDefaultsCheck();
	assert( is( K_LSTICK, "+speed" ));
	/* a key the player set themselves keeps it */
	profile( 5, cof_pad_layout_builtin, ARRAYSIZE( cof_pad_layout_builtin ));
	bindings[K_LSTICK] = "+sprint";
	bindings[K_B_BUTTON] = "cof_warp";
	CL_CoF_PadDefaultsCheck();
	assert( is( K_LSTICK, "+sprint" ) && is( K_B_BUTTON, "cof_warp" ) && is( K_JOY1, "+attack3" ) && is( K_DPAD_DOWN, "weapontoggle" ));
	assert( strstr( printed, "13 of the engine's" ));
	/* a new copy's first launch: no generation yet, the Half-Life binds there (the app's config.cfg has no unbindall) */
	profile( 0, cof_pad_layout_builtin, ARRAYSIZE( cof_pad_layout_builtin ));
	CL_CoF_PadDefaultsCheck();
	assert( the_layout() && strstr( printed, "15 of the engine's" ));
	/* the game's own config.cfg (unbindall): nothing bound, the layout applied */
	profile( 0, NULL, 0 );
	CL_CoF_PadDefaultsCheck();
	assert( the_layout() && strstr( printed, "default layout applied (16 binds, generation 6)" ));
	/* generation 5 already on the layout: kept */
	profile( 5, cof_pad_layout, ARRAYSIZE( cof_pad_layout ));
	CL_CoF_PadDefaultsCheck();
	assert( the_layout() && strstr( printed, "generation 5 layout kept as generation 6" ));
	/* an older generation still migrates as before */
	profile( 4, cof_pad_layout_gen4, ARRAYSIZE( cof_pad_layout_gen4 ));
	CL_CoF_PadDefaultsCheck();
	assert( the_layout() && strstr( printed, "generation 4 layout migrated to generation 6" ) && strstr( printed, "0 Half-Life pad binds moved" ));
	/* a profile bound some other way: left as it is */
	profile( 0, NULL, 0 );
	bindings[K_A_BUTTON] = "+use";
	CL_CoF_PadDefaultsCheck();
	assert( is( K_A_BUTTON, "+use" ) && is( K_LSTICK, "" ) && strstr( printed, "gamepad keys already bound" ));
	puts( "pad defaults scenarios passed" );
	return 0;
}
