/*
cl_scrn.c - refresh screen
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "vgui_draw.h"
#include "qfont.h"
#include "input.h"
#include "library.h"

CVAR_DEFINE_AUTO( scr_centertime, "2.5", 0, "centerprint hold time" );
CVAR_DEFINE_AUTO( scr_loading, "0", 0, "loading bar progress" );
CVAR_DEFINE_AUTO( scr_download, "-1", 0, "downloading bar progress" );
CVAR_DEFINE( scr_viewsize, "viewsize", "120", FCVAR_ARCHIVE, "screen size (quake only)" );
CVAR_DEFINE_AUTO( cl_testlights, "0", FCVAR_CHEAT, "test dynamic lights" );
CVAR_DEFINE( cl_allow_levelshots, "allow_levelshots", "0", FCVAR_ARCHIVE, "allow engine to use indivdual levelshots instead of 'loading' image" );
CVAR_DEFINE_AUTO( cl_levelshot_name, "*black", 0, "contains path to current levelshot" );
static CVAR_DEFINE_AUTO( cl_envshot_size, "256", FCVAR_ARCHIVE, "envshot size of cube side" );
CVAR_DEFINE_AUTO( v_dark, "0", 0, "starts level from dark screen" );
static CVAR_DEFINE_AUTO( net_speeds, "0", FCVAR_ARCHIVE, "show network packets" );
static CVAR_DEFINE_AUTO( cl_showfps, "0", FCVAR_ARCHIVE, "show client fps" );
static CVAR_DEFINE_AUTO( cl_showpos, "0", FCVAR_ARCHIVE, "show local player position and velocity" );
static CVAR_DEFINE_AUTO( cl_showents, "0", FCVAR_ARCHIVE | FCVAR_CHEAT, "show entities information (largely undone)" );

static qboolean SCR_IsSafeCoFRootSaveShotPath( const char *path )
{
	const char *const prefix = "../SAVE/";
	const size_t prefixLength = Q_strlen( prefix );
	const size_t pathLength = Q_strlen( path );

	if( Q_strncmp( path, prefix, prefixLength ) || pathLength <= prefixLength + 4 || Q_stricmp( path + pathLength - 4, ".bmp" ))
		return false;

	for( const char *c = path + prefixLength; c < path + pathLength - 4; c++ )
	{
		if( *c == '/' || *c == '\\' || *c == ':' )
			return false;
	}

	return true;
}
static CVAR_DEFINE_AUTO( cl_showcmd, "0", 0, "visualize usercmd button presses" );

typedef struct
{
	int	x1, y1, x2, y2;
} dirty_t;

static dirty_t	scr_dirty, scr_old_dirty[2];
static qboolean	scr_init = false;

/*
==============
SCR_DrawFPS
==============
*/
void SCR_DrawFPS( int height )
{
	rgba_t		color;
	static double	nexttime = 0, lasttime = 0;
	static double	framerate = 0;
	static int	framecount = 0;
	static int	minfps = 9999;
	static int	maxfps = 0;
	char		fpsstring[64];
	int		offset;

	if( cls.state != ca_active || !cl_showfps.value || cl.background )
		return;

	switch( cls.scrshot_action )
	{
	case scrshot_normal:
	case scrshot_snapshot:
	case scrshot_inactive:
		break;
	default: return;
	}

	double newtime = Platform_DoubleTime();
	if( newtime >= nexttime )
	{
		framerate = framecount / (newtime - lasttime);
		lasttime = newtime;
		nexttime = Q_max( nexttime + 1.0, lasttime - 1.0 );
		framecount = 0;
	}

	float calc = framerate;
	framecount++;

	if( calc < 1.0f )
	{
		if( calc == 0.0f )
			return;

		Q_snprintf( fpsstring, sizeof( fpsstring ), "%4i spf", (int)(1.0f / calc + 0.5f));
		MakeRGBA( color, 255, 0, 0, 255 );
	}
	else
	{
		int	curfps = (int)(calc + 0.5f);

		if( curfps < minfps ) minfps = curfps;
		if( curfps > maxfps ) maxfps = curfps;

		if( cl_showfps.value == 2 )
			Q_snprintf( fpsstring, sizeof( fpsstring ), "fps: ^1%4i min, ^3%4i cur, ^2%4i max", minfps, curfps, maxfps );
		else Q_snprintf( fpsstring, sizeof( fpsstring ), "%4i fps", curfps );
		MakeRGBA( color, 255, 255, 255, 255 );
	}

	Con_DrawStringLen( fpsstring, &offset, NULL );
	Con_DrawString( refState.width - offset - 4, height, fpsstring, color );
}

/*
==============
SCR_DrawPos

Draw local player position, angles and velocity
==============
*/
void SCR_DrawPos( void )
{
	static char     msg[MAX_SYSPATH];
	rgba_t color;

	if( cls.state != ca_active || !cl_showpos.value || cl.background )
		return;

	cl_entity_t *ent = CL_GetLocalPlayer();
	float speed = VectorLength( cl.simvel );

	Q_snprintf( msg, MAX_SYSPATH,
		"pos: %.2f %.2f %.2f\n"
		"ang: %.2f %.2f %.2f\n"
		"velocity: %.2f",
		cl.simorg[0], cl.simorg[1], cl.simorg[2],
		// should we use entity angles or viewangles?
		// view isn't always bound to player
		ent->angles[0], ent->angles[1], ent->angles[2],
		speed );

	MakeRGBA( color, 255, 255, 255, 255 );

	Con_DrawString( refState.width / 2, 4, msg, color );
}

/*
==============
SCR_DrawEnts
==============
*/
void SCR_DrawEnts( void )
{
	rgba_t color = { 255, 255, 255, 255 };

	if( cls.state != ca_active || !cl_showents.value || ( cl.maxclients > 1 && !cls.demoplayback ))
		return;

	// this probably better hook CL_AddVisibleEntities
	// as entities might get added by client.dll
	for( int i = 0; i < clgame.maxEntities; i++ )
	{
		const cl_entity_t *ent = &clgame.entities[i];
		string msg;
		vec3_t screen;

		if( ent->curstate.messagenum != cl.parsecount )
			continue;

		vec3_t pos = Vec3( ent->origin );

		if( ent->model != NULL )
		{
			vec3_t v;

			// simple model type filter
			if( cl_showents.value > 1 )
			{
				if( ent->model->type != (modtype_t)( cl_showents.value - 2 ))
					continue;
			}

			VectorAverage( ent->model->mins, ent->model->maxs, v );
			VectorAdd( pos, v, pos );
		}

		if( !ref.dllFuncs.WorldToScreen( pos, screen ))
		{
			Q_snprintf( msg, sizeof( msg ),
				"entity %d\n"
				"model %s\n"
				"movetype %d\n",
				ent->index,
				ent->model ? ent->model->name : "(null)",
				ent->curstate.movetype );

			screen[0] =  0.5f * screen[0] * refState.width;
			screen[1] = -0.5f * screen[1] * refState.height;
			screen[0] += 0.5f * refState.width;
			screen[1] += 0.5f * refState.height;

			Con_DrawString( screen[0], screen[1], msg, color );
		}
	}
}

/*
==============
SCR_DrawUserCmd

another debugging aids, shows pressed buttons
==============
*/
void SCR_DrawUserCmd( void )
{
	runcmd_t *pcmd = &cl.commands[( cls.netchan.outgoing_sequence - 1 ) & CL_UPDATE_MASK];
	struct
	{
		int mask;
		const char *name;
	} buttons[16] =
	{
	{ IN_ATTACK, "attack" },
	{ IN_JUMP, "jump" },
	{ IN_DUCK, "duck" },
	{ IN_FORWARD, "forward" },
	{ IN_BACK, "back" },
	{ IN_USE, "use" },
	{ IN_CANCEL, "cancel" },
	{ IN_LEFT, "left" },
	{ IN_RIGHT, "right" },
	{ IN_MOVELEFT, "moveleft" },
	{ IN_MOVERIGHT, "moveright" },
	{ IN_ATTACK2, "attack2" },
	{ IN_RUN, "run" },
	{ IN_RELOAD, "reload" },
	{ IN_ALT1, "alt1" },
	{ IN_SCORE, "score" },
	};
	cl_font_t *font = Con_GetCurFont();
	string msg;
	int ypos = 100;

	if( cls.state != ca_active || !cl_showcmd.value )
		return;

	for( int i = 0; i < ARRAYSIZE( buttons ); i++ )
	{
		rgba_t rgba;

		rgba[0] = FBitSet( pcmd->cmd.buttons, buttons[i].mask ) ? 0 : 255;
		rgba[1] = FBitSet( pcmd->cmd.buttons, buttons[i].mask ) ? 255 : 0;
		rgba[2] = 0;
		rgba[3] = 255;

		Con_DrawString( 100, ypos, buttons[i].name, rgba );

		ypos += font->charHeight;
	}

	Q_snprintf( msg, sizeof( msg ),
		"F/S/U: %g %g %g\n"
		"impulse: %u\n"
		"msec: %u",
		pcmd->cmd.forwardmove, pcmd->cmd.sidemove, pcmd->cmd.upmove,
		pcmd->cmd.impulse,
		pcmd->cmd.msec );
	Con_DrawString( 100, ypos, msg, g_color_table[7] );
}

/*
==============
SCR_NetSpeeds

same as r_speeds but for network channel
==============
*/
void SCR_NetSpeeds( void )
{
	static char	msg[MAX_SYSPATH];
	float		time = cl.mtime[0];
	static int	min_svfps = 100;
	static int	max_svfps = 0;
	int		cur_svfps = 0;
	static int	min_clfps = 100;
	static int	max_clfps = 0;
	int		cur_clfps = 0;
	rgba_t		color;
	cl_font_t *font = Con_GetCurFont();

	if( !host.allow_console )
		return;

	if( !net_speeds.value || cls.state != ca_active )
		return;

	// prevent to get too big values at max
	if( cl_serverframetime() > 0.0001f )
	{
		cur_svfps = Q_rint( 1.0f / cl_serverframetime( ));
		if( cur_svfps < min_svfps ) min_svfps = cur_svfps;
		if( cur_svfps > max_svfps ) max_svfps = cur_svfps;
	}

	// prevent to get too big values at max
	if( cl_clientframetime() > 0.0001f )
	{
		cur_clfps = Q_rint( 1.0f / cl_clientframetime( ));
		if( cur_clfps < min_clfps ) min_clfps = cur_clfps;
		if( cur_clfps > max_clfps ) max_clfps = cur_clfps;
	}

	Q_snprintf( msg, sizeof( msg ),
		"Updaterate: ^1%2i min, ^3%2i cur, ^2%2i max\n"
		"Client FPS: ^1%i min, ^3%3i cur, ^2%3i max\n"
		"Game Time: %02d:%02d\n"
		"Total received from server: %s\n"
		"Total sent to server: %s\n",
		min_svfps, cur_svfps, max_svfps,
		min_clfps, cur_clfps, max_clfps,
		(int)(time / 60.0f ), (int)fmod( time, 60.0f ),
		Q_memprint( cls.netchan.total_received ),
		Q_memprint( cls.netchan.total_sended )
	);

	// milestone 4 (cof_text_autoscale): 320 is where this block starts with the
	// stock 8 px glyphs. Once the console font is sized from the display, the
	// widest line no longer fits in 320 scaled pixels and the text ran off the
	// right edge, so place it from its own measured width instead.
	int width = 0, x, y = 384;

	CL_DrawStringLen( font, msg, &width, NULL, FONT_DRAW_RESETCOLORONLF );
	if( width <= 0 )
		width = 320 * font->scale;

	x = Q_max( 0, refState.width - width - Q_max( 4, font->charWidths[' ' ] ));

	MakeRGBA( color, 255, 255, 255, 255 );
	CL_DrawString( x, y, msg, color, font, FONT_DRAW_RESETCOLORONLF );
}

/*
================
SCR_RSpeeds
================
*/
void SCR_RSpeeds( void )
{
	char	msg[2048];

	if( !host.allow_console )
		return;

	if( ref.dllFuncs.R_SpeedsMessage( msg, sizeof( msg )))
	{
		rgba_t	color;
		cl_font_t *font = Con_GetCurFont();

		// milestone 4 (cof_text_autoscale): same as SCR_NetSpeeds above, 340 is
		// a stock-glyph constant; measure the block and right-align it
		int width = 0, x, y = 64;

		CL_DrawStringLen( font, msg, &width, NULL, FONT_DRAW_RESETCOLORONLF );
		if( width <= 0 )
			width = 340 * font->scale;

		x = Q_max( 0, refState.width - width - Q_max( 4, font->charWidths[' ' ] ));

		MakeRGBA( color, 255, 255, 255, 255 );
		CL_DrawString( x, y, msg, color, font, FONT_DRAW_RESETCOLORONLF );
	}
}

/*
================
SCR_MakeLevelShot

creates levelshot at next frame
================
*/
void SCR_MakeLevelShot( void )
{
	if( cls.scrshot_request != scrshot_plaque )
		return;

	// make levelshot at nextframe()
	Cbuf_AddText( "levelshot\n" );
}

/*
===============
VID_WriteOverviewScript

Create overview script file
===============
*/
static void VID_WriteOverviewScript( void )
{
	ref_overview_t	*ov = &clgame.overView;
	string		filename;

	Q_snprintf( filename, sizeof( filename ), "overviews/%s.txt", clgame.mapname );

	file_t		*f = FS_Open( filename, "w", false );
	if( !f )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, filename );
		return;
	}

	FS_Printf( f, "// overview description file for %s.bsp\n\n", clgame.mapname );
	FS_Print( f, "global\n{\n" );
	FS_Printf( f, "\tZOOM\t%.2f\n", ov->flZoom );
	FS_Printf( f, "\tORIGIN\t%.2f\t%.2f\t%.2f\n", ov->origin[0], ov->origin[1], ov->origin[2] );
	FS_Printf( f, "\tROTATED\t%i\n", ov->rotated ? 1 : 0 );
	FS_Print( f, "}\n\nlayer\n{\n" );
	FS_Printf( f, "\tIMAGE\t\"overviews/%s.bmp\"\n", clgame.mapname );
	FS_Printf( f, "\tHEIGHT\t%.2f\n", ov->zFar );	// ???
	FS_Print( f, "}\n" );

	FS_Close( f );
}

/*
================
SCR_MakeScreenShot

create a requested screenshot type
================
*/
void SCR_MakeScreenShot( void )
{
	qboolean	iRet = false;
	int	viewsize;

	if( cls.scrshot_action == scrshot_inactive )
		return;

	if( cls.envshot_viewsize > 0 )
		viewsize = cls.envshot_viewsize;
	else viewsize = cl_envshot_size.value;

	V_CheckGamma();

	switch( cls.scrshot_action )
	{
	case scrshot_normal:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_SCREENSHOT );
		break;
	case scrshot_snapshot:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_SNAPSHOT );
		break;
	case scrshot_plaque:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_LEVELSHOT );
		break;
	case scrshot_savegame:
		if( Cvar_VariableInteger( "cof_save_root_compat" ) && GI && !Q_stricmp( GI->gamefolder, "cryoffear" ))
		{
			if( !SCR_IsSafeCoFRootSaveShotPath( cls.shotname ))
			{
				Con_Printf( S_ERROR "Refusing unsafe Cry of Fear saveshot path: %s\n", cls.shotname );
				break;
			}

			FS_AllowDirectPaths( true );
		}
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_MINISHOT );
		FS_AllowDirectPaths( false );
		break;
	case scrshot_envshot:
		iRet = ref.dllFuncs.VID_CubemapShot( cls.shotname, viewsize, cls.envshot_vieworg, false );
		break;
	case scrshot_skyshot:
		iRet = ref.dllFuncs.VID_CubemapShot( cls.shotname, viewsize, cls.envshot_vieworg, true );
		break;
	case scrshot_mapshot:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_MAPSHOT );
		if( iRet )
			VID_WriteOverviewScript(); // store overview script too
		break;
	}

	// report
	if( iRet )
	{
		// snapshots don't writes message about image
		if( cls.scrshot_action != scrshot_snapshot )
			Con_Reportf( "Write %s\n", cls.shotname );
	}
	else Con_Printf( S_ERROR "Unable to write %s\n", cls.shotname );

	cls.envshot_vieworg = NULL;
	cls.scrshot_action = scrshot_inactive;
	cls.envshot_disable_vis = false;
	cls.envshot_viewsize = 0;
	cls.shotname[0] = '\0';
}

/*
================
SCR_DrawPlaque
================
*/
static qboolean SCR_DrawPlaque( void )
{
	if(( cl_allow_levelshots.value && !cls.changelevel ) || cl.background )
	{
		int levelshot = ref.dllFuncs.GL_LoadTexture( cl_levelshot_name.string, NULL, 0, TF_IMAGE );
		ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
		ref.dllFuncs.R_DrawStretchPic( 0, 0, refState.width, refState.height, 0, 0, 1, 1, levelshot );
		if( !cl.background ) CL_DrawHUD( CL_LOADING );

		return true;
	}

	return false;
}

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque( qboolean is_background )
{
	S_StopAllSounds( true );
	cl.audio_prepped = false;			// don't play ambients

	if( cls.key_dest == key_menu && !cls.changedemo && !is_background )
	{
		UI_SetActiveMenu( false );
		if( cls.state == ca_disconnected && !(GameState->curstate == STATE_RUNFRAME && GameState->nextstate != STATE_RUNFRAME) )
			SCR_UpdateScreen();
	}

	if( cls.state == ca_disconnected || cls.disable_screen )
		return; // already set

	if( cls.key_dest == key_console )
		return;

	if( is_background ) IN_MouseSavePos( );
	cls.draw_changelevel = !is_background;
	SCR_UpdateScreen();

	// set video_prepped after update screen, so engine can draw last remaining frame
	cl.video_prepped = false;
	cls.disable_screen = host.realtime;
	cl.background = is_background;		// set right state before svc_serverdata is came

//	SNDDMA_LockSound();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque( void )
{
	cls.disable_screen = 0.0f;
	Con_ClearNotify();
//	SNDDMA_UnlockSound();
}

/*
=================
SCR_AddDirtyPoint
=================
*/
static void SCR_AddDirtyPoint( int x, int y )
{
	if( x < scr_dirty.x1 ) scr_dirty.x1 = x;
	if( x > scr_dirty.x2 ) scr_dirty.x2 = x;
	if( y < scr_dirty.y1 ) scr_dirty.y1 = y;
	if( y > scr_dirty.y2 ) scr_dirty.y2 = y;
}

/*
================
SCR_DirtyScreen
================
*/
void SCR_DirtyScreen( void )
{
	SCR_AddDirtyPoint( 0, 0 );
	SCR_AddDirtyPoint( refState.width - 1, refState.height - 1 );
}

static void R_DrawTileClear( int texnum, int x, int y, int w, int h, float tw, float th )
{
	float s1 = x / tw;
	float t1 = y / th;
	float s2 = ( x + w ) / tw;
	float t2 = ( y + h ) / th;

	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, texnum );
}

/*
================
SCR_TileClear
================
*/
void SCR_TileClear( void )
{
	int	i, top, bottom, left, right, texnum;
	dirty_t	clear;

	if( likely( scr_viewsize.value >= 120 ))
		return; // full screen rendering

	if( !cls.tileImage )
	{
		cls.tileImage = ref.dllFuncs.GL_LoadTexture( "gfx/backtile.lmp", NULL, 0, TF_NOMIPMAP );
		if( !cls.tileImage )
			cls.tileImage = -1;
	}

	if( cls.tileImage > 0 )
		texnum = cls.tileImage;
	else texnum = 0;

	// erase rect will be the union of the past three frames
	// so tripple buffering works properly
	clear = scr_dirty;

	for( i = 0; i < 2; i++ )
	{
		if( scr_old_dirty[i].x1 < clear.x1 )
			clear.x1 = scr_old_dirty[i].x1;
		if( scr_old_dirty[i].x2 > clear.x2 )
			clear.x2 = scr_old_dirty[i].x2;
		if( scr_old_dirty[i].y1 < clear.y1 )
			clear.y1 = scr_old_dirty[i].y1;
		if( scr_old_dirty[i].y2 > clear.y2 )
			clear.y2 = scr_old_dirty[i].y2;
	}

	scr_old_dirty[1] = scr_old_dirty[0];
	scr_old_dirty[0] = scr_dirty;

	scr_dirty.x1 = 9999;
	scr_dirty.x2 = -9999;
	scr_dirty.y1 = 9999;
	scr_dirty.y2 = -9999;

	if( clear.y2 <= clear.y1 )
		return; // nothing disturbed

	top = clgame.viewport[1];
	bottom = top + clgame.viewport[3] - 1;
	left = clgame.viewport[0];
	right = left + clgame.viewport[2] - 1;

	float tw = REF_GET_PARM( PARM_TEX_SRC_WIDTH, texnum );
	float th = REF_GET_PARM( PARM_TEX_SRC_HEIGHT, texnum );

	if( clear.y1 < top )
	{
		// clear above view screen
		i = clear.y2 < top-1 ? clear.y2 : top - 1;
		R_DrawTileClear( texnum, clear.x1, clear.y1, clear.x2 - clear.x1 + 1, i - clear.y1 + 1, tw, th );
		clear.y1 = top;
	}

	if( clear.y2 > bottom )
	{
		// clear below view screen
		i = clear.y1 > bottom + 1 ? clear.y1 : bottom + 1;
		R_DrawTileClear( texnum, clear.x1, i, clear.x2 - clear.x1 + 1, clear.y2 - i + 1, tw, th );
		clear.y2 = bottom;
	}

	if( clear.x1 < left )
	{
		// clear left of view screen
		i = clear.x2 < left - 1 ? clear.x2 : left - 1;
		R_DrawTileClear( texnum, clear.x1, clear.y1, i - clear.x1 + 1, clear.y2 - clear.y1 + 1, tw, th );
		clear.x1 = left;
	}

	if( clear.x2 > right )
	{
		// clear left of view screen
		i = clear.x1 > right + 1 ? clear.x1 : right + 1;
		R_DrawTileClear( texnum, i, clear.y1, clear.x2 - i + 1, clear.y2 - clear.y1 + 1, tw, th );
		clear.x2 = right;
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void )
{
	qboolean screen_redraw = true; // assume screen has been redrawn

	if( !V_PreRender( )) return;

	switch( cls.state )
	{
	case ca_disconnected:
		Con_RunConsole ();
		break;
	case ca_connecting:
	case ca_connected:
	case ca_validate:
		screen_redraw = SCR_DrawPlaque();
		break;
	case ca_active:
		Con_RunConsole ();
		V_RenderView();
		break;
	case ca_cinematic:
		SCR_DrawCinematic();
		break;
	default:
		Host_Error( "%s: bad cls.state\n", __func__ );
		break;
	}

	// during changelevel we might have a few frames when we have nothing to draw
	// (assuming levelshots are off) and drawing 2d on top of nothing or cleared screen
	// is ugly, specifically with Adreno and ImgTec GPUs
	if( screen_redraw || !cls.changelevel || !cls.changedemo )
		V_PostRender();
}

/*
================
SCR_LoadCreditsFont

INTERNAL RESOURCE
================
*/
// Cry of Fear engine HUD text sizing: the target height cls.creditsFont was
// last loaded for, so SCR_VidInit() can tell when the screen has moved away
// from it. A negative sentinel compares unequal to every live target, so the
// first VidInit always loads.
static float scr_hud_text_target = -1.0f;

// Cry of Fear engine HUD text legibility (cof_hud_text_font, milestone 4b).
// The game's own gfx/creditsfont.fnt is a thin, aliased, one-size bitmap: at
// 1440p and 2160p it is magnified 1.3x and 2x and the stems fall apart, which
// is half of "the subtitles are too light" in the user's feedback. These are
// the base heights of the generated Inter SemiBold atlases shipped as
// gamedata/cryoffear/fonts/cof_hudtext<N>.fnt, produced by
// scripts/make-cof-console-font.py. Two sizes, not more: the .fnt container
// addresses glyphs with a 16-bit linear pixel offset, so the whole atlas has
// to fit in 65536 pixels and a base above ~20 px starts dropping glyphs out of
// the 216-character cp1252 set - which the language packs need.
static const int scr_hud_text_atlas_base[] = { 14, 19 };
static qboolean scr_hud_text_atlas_warned;

// the height the game's own HUD font has on this display, before any resizing.
// The vertical correction in cl_game.c needs it to know how far the client's
// own centring has drifted; 0 means "not known yet".
float cls_cof_hud_text_shipped;

/*
================
SCR_HudTextUseAtlas

Gated on the Cry of Fear client like the rest of the cof_* stack, so no other
game's HUD font is touched, and on cof_hud_text_font so the game's own font is
always one cvar away.
================
*/
static qboolean SCR_HudTextUseAtlas( void )
{
	if( cof_hud_text_font.value == 0.0f )
		return false;

	if( !GI || Q_stricmp( GI->gamefolder, "cryoffear" ))
		return false;

	return true;
}

/*
================
SCR_HudTextTargetHeight

Cry of Fear engine HUD text sizing (cof_hud_text_height_pct, milestone 4).

cls.creditsFont is the font behind pfnDrawCharacter, which is how the Cry of
Fear client draws its hints, its "Using Gas Mask" style prompts and its
subtitles: the client measures the text itself with gHUD.m_scrinfo.charWidths[]
and iCharHeight and emits one pfnDrawCharacter per glyph. Those two fields are
copied out of this very font at the bottom of SCR_LoadCreditsFont(), so making
the font bigger makes the client's own centring, line pitch and wrap points
follow it for free - no client-side layout has to be understood or touched.

Same model as the console's cof_text_autoscale: a plain fraction of the render
height, continuous, with no per-resolution cases. The default 1.85 percent
reproduces the stock 20 px glyph at 1920x1080 and scales it from there, so a
1080p player sees exactly what they saw before and a 4K player finally gets
readable subtitles. hud_fontscale multiplies it, so a user who already set that
keeps their preference on top. Returns 0 for "size it exactly as stock".
================
*/
static float SCR_HudTextTargetHeight( void )
{
	float	px;

	if( cof_hud_text_height_pct.value <= 0.0f )
		return 0.0f;

	if( !ref.initialized || refState.height <= 0 )
		return 0.0f;

	px = refState.height * bound( 0.4f, cof_hud_text_height_pct.value, 12.0f ) * 0.01f;

	if( hud_fontscale.value > 0.0f )
		px *= hud_fontscale.value;

	// the menu's HUD scale control, the same multiplier the surface transform
	// uses, so text and panels grow together
	px *= CL_CoF_UIScaleUser();

	return bound( 6.0f, px, 192.0f );
}

/*
================
SCR_ScaleCreditsFont

Resize the loaded HUD font to the target height. The atlas was loaded at
base_scale, so dividing it back out recovers the raw metrics and setting
font->scale is exactly what loading at that scale would have produced -
CL_DrawCharacter() multiplies both the glyph rectangle and the advance by it.
================
*/
static void SCR_ScaleCreditsFont( cl_font_t *font, float base_scale, float shipped )
{
	float	target = SCR_HudTextTargetHeight();
	float	base, scale;

	if( !font->valid )
		return;

	if( target <= 0.0f )
	{
		// stock sizing asked for. With the game's own font that means "leave it
		// exactly as loaded", which is what the early return does. With the
		// generated atlas there is nothing to leave alone - its base height is
		// an arbitrary 14 or 19 px - so bring it to the height the game's own
		// font would have had.
		if( !SCR_HudTextUseAtlas( ))
			return;

		target = shipped;
	}

	// never smaller than the font the game shipped, for the same reason
	// cof_ui_scale never goes below 1: the shipped font is already small at
	// 1080p and shrinking it at 720p would be a regression, not a feature. The
	// floor used to be expressed as "glyph scale never below 1", which is the
	// same thing only while the atlas IS the shipped font; with a generated
	// atlas of its own base height it has to be said in pixels.
	if( shipped > 0.0f && target < shipped )
		target = shipped;

	if( base_scale <= 0.0f )
		base_scale = 1.0f;

	base = font->charHeight / base_scale;

	if( base <= 0.0f )
		return;

	scale = target / base;

	if( scale <= 0.0f )
		return;

	font->scale = scale;
	font->charHeight = Q_max( 1, Q_rint( base * scale ));

	for( int i = 0; i < ARRAYSIZE( font->charWidths ); i++ )
	{
		if( font->charWidths[i] > 0 )
			font->charWidths[i] = Q_max( 1, Q_rint(( font->charWidths[i] / base_scale ) * scale ));
	}

	scr_hud_text_target = SCR_HudTextTargetHeight();

	Con_Reportf( "[cof-hudtext] %ix%i: target %.1f px (cof_hud_text_height_pct %.2f, hud_fontscale %.2f, shipped %.0f px) -> base %.0f px, glyph scale %.3f, charH %i px\n",
		refState.width, refState.height, target, cof_hud_text_height_pct.value,
		hud_fontscale.value, shipped, base, scale, font->charHeight );
}

void SCR_LoadCreditsFont( void )
{
	cl_font_t *const font = &cls.creditsFont;
	qboolean success = false;
	float scale = hud_fontscale.value;
	float shipped = 0.0f;
	dword crc = 0;

	// Cry of Fear engine HUD text sizing: SCR_ScaleCreditsFont() below rewrites
	// charHeight and charWidths[] in place, so the loader has to start from a
	// clean font or a second call would scale the already scaled metrics. This
	// is a no-op for a font that is not loaded.
	CL_FreeFont( font );

	// replace default gfx.wad textures by current charset's font
	if( !CRC32_File( &crc, "gfx.wad" ) || crc == 0x49eb9f16 )
	{
		string charsetFnt;

		if( Q_snprintf( charsetFnt, sizeof( charsetFnt ),
			"creditsfont_%s.fnt", Cvar_VariableString( "con_charset" )) > 0 )
		{
			if( FS_FileExists( charsetFnt, false ))
				success = Con_LoadVariableWidthFont( charsetFnt, font, scale, &hud_fontrender, TF_FONT );
		}
	}

	if( !success )
		success = Con_LoadVariableWidthFont( "gfx/creditsfont.fnt", font, scale, &hud_fontrender, TF_FONT );

	// it's a Quake font, we need to keep source texture for it
	if( !success )
		success = Con_LoadFixedWidthFont( "gfx/conchars", font, scale, &hud_fontrender, TF_FONT|TF_NEAREST|TF_KEEP_SOURCE );

	// the height the game's own font has on this display, before any resizing.
	// It is the floor the target is never allowed below, and with the generated
	// atlas in play it is the only way left to know what "the shipped size" was.
	shipped = success ? ( font->charHeight / ( scale > 0.0f ? scale : 1.0f )) : 0.0f;
	cls_cof_hud_text_shipped = shipped;

	// Cry of Fear engine HUD text legibility (cof_hud_text_font): swap the
	// game's thin one-size bitmap for the generated Inter SemiBold atlas whose
	// base height is nearest below what this display wants, so the glyphs are
	// magnified as little as possible instead of 2x at 2160p. Loaded into a
	// scratch font first: a missing or corrupt atlas then leaves the font the
	// stock chain already produced completely untouched.
	if( SCR_HudTextUseAtlas( ))
	{
		float want = SCR_HudTextTargetHeight();
		cl_font_t atlas;
		string path;
		int page;

		if( want < shipped ) want = shipped;
		if( want <= 0.0f ) want = (float)scr_hud_text_atlas_base[0];

		for( page = (int)ARRAYSIZE( scr_hud_text_atlas_base ) - 1; page > 0; page-- )
		{
			if( want >= (float)scr_hud_text_atlas_base[page] )
				break;
		}

		memset( &atlas, 0, sizeof( atlas ));
		Q_snprintf( path, sizeof( path ), cof_hud_text_style.value ? "fonts/cof_hudtext_bold%i.fnt" : "fonts/cof_hudtext%i.fnt", page );
		// A partial installation keeps the earlier atlas rather than losing text.
		if( cof_hud_text_style.value && !FS_FileExists( path, false ))
			Q_snprintf( path, sizeof( path ), "fonts/cof_hudtext%i.fnt", page );

		if( Con_LoadVariableWidthFont( path, &atlas, scale, &hud_fontrender, TF_FONT ))
		{
			CL_FreeFont( font );
			*font = atlas;
			success = true;

			Con_Reportf( "[cof-hudtext] %s (base %i px) replaces the game font (%.0f px) for a %.1f px target\n",
				path, scr_hud_text_atlas_base[page], shipped, want );
		}
		else
		{
			CL_FreeFont( &atlas );

			if( !scr_hud_text_atlas_warned )
			{
				scr_hud_text_atlas_warned = true;
				Con_Printf( S_WARN "cof_hud_text_font: %s is missing, keeping the game's own HUD font\n", path );
			}
		}
	}

	// Cry of Fear engine HUD text sizing: resize BEFORE the metrics are copied
	// out, so the client lays its hints and subtitles out at the new size
	SCR_ScaleCreditsFont( font, scale, shipped );

	// copy font size for client.dll
	if( success )
	{
		clgame.scrInfo.iCharHeight = cls.creditsFont.charHeight;

		for( int i = 0; i < ARRAYSIZE( cls.creditsFont.charWidths ); i++ )
			clgame.scrInfo.charWidths[i] = cls.creditsFont.charWidths[i];
	}
	else Con_DPrintf( S_ERROR "failed to load HUD font\n" );
}

/*
================
SCR_InstallParticlePalette

INTERNAL RESOURCE
================
*/
static void SCR_InstallParticlePalette( void )
{
	rgbdata_t	*pic;
	int	i;

	// first check 'palette.lmp' then 'palette.pal'
	pic = FS_LoadImage( DEFAULT_INTERNAL_PALETTE, NULL, 0 );
	if( !pic ) pic = FS_LoadImage( DEFAULT_EXTERNAL_PALETTE, NULL, 0 );

	// NOTE: imagelib required this fakebuffer for loading internal palette
	if( !pic ) pic = FS_LoadImage( "#valve.pal", (byte *)&i, 768 );

	if( pic )
	{
		for( i = 0; i < 256; i++ )
		{
			clgame.palette[i].r = pic->palette[i*4+0];
			clgame.palette[i].g = pic->palette[i*4+1];
			clgame.palette[i].b = pic->palette[i*4+2];
		}
		FS_FreeImage( pic );
	}
	else
	{
		// someone deleted internal palette from code...
		for( i = 0; i < 256; i++ )
		{
			clgame.palette[i].r = i;
			clgame.palette[i].g = i;
			clgame.palette[i].b = i;
		}
	}
}

int SCR_LoadPauseIcon( void )
{
	int texnum = 0;

	if( FS_FileExists( "gfx/paused.lmp", false ))
		texnum = ref.dllFuncs.GL_LoadTexture( "gfx/paused.lmp", NULL, 0, TF_IMAGE|TF_ALLOW_NEAREST );
	else if( FS_FileExists( "gfx/pause.lmp", false ))
		texnum = ref.dllFuncs.GL_LoadTexture( "gfx/pause.lmp", NULL, 0, TF_IMAGE|TF_ALLOW_NEAREST );

	return texnum ? texnum : -1;
}

/*
================
SCR_RegisterTextures

INTERNAL RESOURCE
================
*/
void SCR_RegisterTextures( void )
{
	const char *exts[] = { "lmp", "bmp", "png" };
	const char *names[] = { "gfx/lambda", "gfx/loading" };
	uint flags = TF_IMAGE|TF_ALLOW_NEAREST;

	if( cl_allow_levelshots.value )
		SetBits( flags, TF_LUMINANCE );

	for( int i = 0; i < ARRAYSIZE( names ); i++ )
	{
		for( int j = 0; j < ARRAYSIZE( exts ); j++ )
		{
			string path;

			if( Q_snprintf( path, sizeof( path ), "%s.%s", names[i], exts[j] ) < 0 )
				continue;

			if( !FS_FileExists( path, false ))
				continue;

			cls.loadingBar = ref.dllFuncs.GL_LoadTexture( path, NULL, 0, flags );

			if( cls.loadingBar )
				return;
		}
	}

	Con_Printf( S_WARN "%s: failed to load loading image\n", __func__ );
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f( void )
{
	Cvar_SetValue( "viewsize", Q_min( scr_viewsize.value + 10, 120 ));
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f( void )
{
	Cvar_SetValue( "viewsize", Q_max( scr_viewsize.value - 10, 30 ));
}

/*
==================
SCR_VidInit
==================
*/
void SCR_VidInit( void )
{
	if( !ref.initialized ) // don't call VidInit too soon
		return;

	memset( &clgame.ds, 0, sizeof( clgame.ds )); // reset a draw state
	memset( &gameui.ds, 0, sizeof( gameui.ds )); // reset a draw state
	memset( &clgame.centerPrint, 0, sizeof( clgame.centerPrint ));

	// update screen sizes for menu
	if( gameui.globals )
	{
		gameui.globals->scrWidth = refState.width;
		gameui.globals->scrHeight = refState.height;
	}

	// Cry of Fear in-game UI scaling: pin the size the client's one and only
	// layout pass is about to be made at, before anything can read it
	CL_CoF_UIPinLayout();

	// Cry of Fear engine HUD text sizing: the target is a fraction of the render
	// height, so a mode change has to reload the font at the new size. The
	// client re-reads charWidths[]/iCharHeight from pfnGetScreenInfo in its own
	// VidInit, which runs below, so the new metrics are in place in time
	// which atlas is loaded is not a function of the target height, so
	// cof_hud_text_font has to force the reload on its own
	if( scr_hud_text_target != SCR_HudTextTargetHeight( ) || FBitSet( cof_hud_text_font.flags, FCVAR_CHANGED ) || FBitSet( cof_hud_text_style.flags, FCVAR_CHANGED ))
	{
		ClearBits( cof_hud_text_font.flags, FCVAR_CHANGED );
		ClearBits( cof_hud_text_style.flags, FCVAR_CHANGED );
		CL_FreeFont( &cls.creditsFont );
		SCR_LoadCreditsFont();
	}

	// notify vgui about screen size change
	if( clgame.hInstance )
	{
		VGui_Startup( refState.width, refState.height );
	}

	CL_ClearSpriteTextures(); // now all hud sprites are invalid

	// vid_state has changed
	if( gameui.hInstance ) gameui.dllFuncs.pfnVidInit();
	if( clgame.hInstance ) clgame.dllFuncs.pfnVidInit();

	// restart console size
	Con_VidInit ();
	Touch_NotifyResize();
}

/*
==================
SCR_Init
==================
*/
void SCR_Init( void )
{
	if( scr_init ) return;

	Cvar_RegisterVariable( &scr_centertime );
	Cvar_RegisterVariable( &cl_levelshot_name );
	Cvar_RegisterVariable( &cl_allow_levelshots );
	Cvar_RegisterVariable( &scr_loading );
	Cvar_RegisterVariable( &scr_download );
	Cvar_RegisterVariable( &cl_testlights );
	Cvar_RegisterVariable( &cl_envshot_size );
	Cvar_RegisterVariable( &v_dark );
	Cvar_RegisterVariable( &scr_viewsize );
	Cvar_RegisterVariable( &net_speeds );
	Cvar_RegisterVariable( &cl_showfps );
	Cvar_RegisterVariable( &cl_showpos );
	Cvar_RegisterVariable( &cl_showcmd );
#ifdef _DEBUG
	Cvar_RegisterVariable( &cl_showents );
#endif // NDEBUG

	// register our commands
	Cmd_AddCommand( "skyname", CL_SetSky_f, "set new skybox by basename" );
	Cmd_AddCommand( "loadsky", CL_SetSky_f, "set new skybox by basename" );
	Cmd_AddCommand( "viewpos", SCR_Viewpos_f, "prints current player origin" );
	Cmd_AddCommand( "sizeup", SCR_SizeUp_f, "screen size up to 10 points" );
	Cmd_AddCommand( "sizedown", SCR_SizeDown_f, "screen size down to 10 points" );

	if( !UI_LoadProgs( ))
	{
		Con_Printf( S_ERROR "can't initialize gameui DLL: %s\n", COM_GetLibraryError() ); // there is non fatal for us
		host.allow_console = true; // we need console, because menu is missing
	}

	SCR_VidInit();
	SCR_LoadCreditsFont ();
	SCR_RegisterTextures ();
	SCR_InstallParticlePalette ();
	SCR_InitCinematic();
	CL_InitNetgraph();

	if( host.allow_console && Sys_CheckParm( "-toconsole" ))
		Cbuf_AddText( "toggleconsole\n" );
	else UI_SetActiveMenu( true );

	scr_init = true;
}

void SCR_Shutdown( void )
{
	if( !scr_init ) return;

	Cmd_RemoveCommand( "skyname" );
	Cmd_RemoveCommand( "viewpos" );
	UI_SetActiveMenu( false );
	UI_UnloadProgs();

	scr_init = false;
}
