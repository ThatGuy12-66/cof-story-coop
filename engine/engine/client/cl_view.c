/*
cl_view.c - player rendering positioning
Copyright (C) 2009 Uncle Mike

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
#include "const.h"
#include "entity_types.h"
#include "vgui_draw.h"
#include "sound.h"
#include "input.h" // touch
#include "platform/platform.h" // GL_UpdateSwapInterval

/*
===============
V_CalcViewRect

calc frame rectangle (Quake1 style)
===============
*/
static void V_CalcViewRect( void )
{
	qboolean	full = false;
	int	sb_lines;
	float	size;

	if( Host_IsQuakeCompatible( ))
	{
		// intermission is always full screen
		if( cl.intermission ) size = 120.0f;
		else size = scr_viewsize.value;

		if( size >= 120.0f )
			sb_lines = 0;		// no status bar at all
		else if( size >= 110.0f )
			sb_lines = 24;		// no inventory
		else sb_lines = 48;

		if( scr_viewsize.value >= 100.0f )
		{
			full = true;
			size = 100.0f;
		}
		else size = scr_viewsize.value;

		if( cl.intermission )
		{
			size = 100.0f;
			sb_lines = 0;
			full = true;
		}
		size /= 100.0f;
	}
	else
	{
		full = true;
		sb_lines = 0;
		size = 1.0f;
	}

	clgame.viewport[2] = refState.width * size;
	clgame.viewport[3] = refState.height * size;

	if( clgame.viewport[3] > refState.height - sb_lines )
		clgame.viewport[3] = refState.height - sb_lines;
	if( clgame.viewport[3] > refState.height )
		clgame.viewport[3] = refState.height;

	clgame.viewport[0] = ( refState.width - clgame.viewport[2] ) / 2;
	if( full ) clgame.viewport[1] = 0;
	else clgame.viewport[1] = ( refState.height - sb_lines - clgame.viewport[3] ) / 2;

}

/*
===============
V_SetupViewModel
===============
*/
static void V_SetupViewModel( void )
{
	cl_entity_t	*view = &clgame.viewent;
	player_info_t	*info = &cl.players[cl.playernum];

	if( !cl.local.weaponstarttime )
		cl.local.weaponstarttime = cl.time;

	// setup the viewent variables
	view->curstate.colormap = (info->topcolor & 0xFFFF)|((info->bottomcolor << 8) & 0xFFFF);
	view->curstate.number = cl.playernum + 1;
	view->index = cl.playernum + 1;
	view->model = CL_ModelHandle( cl.local.viewmodel );
	view->curstate.modelindex = cl.local.viewmodel;
	view->curstate.sequence = cl.local.weaponsequence;
	view->curstate.rendermode = kRenderNormal;

	// alias models has another animation methods
	if( view->model && view->model->type == mod_studio )
	{
		view->curstate.animtime = cl.local.weaponstarttime;
		view->curstate.frame = 0.0f;
	}
}

/*
===============
V_SetRefParams
===============
*/
static void V_SetRefParams( ref_params_t *fd )
{
	memset( fd, 0, sizeof( ref_params_t ));

	// probably this is not needs
	VectorCopy( refState.vieworg, fd->vieworg );
	VectorCopy( refState.viewangles, fd->viewangles );

	fd->frametime = host.frametime;
	fd->time = cl.time;

	fd->intermission = cl.intermission;
	fd->paused = (cl.paused != 0);
	fd->spectator = (cls.spectator != 0);
	fd->onground = (cl.local.onground != -1);
	fd->waterlevel = cl.local.waterlevel;

	VectorCopy( cl.simvel, fd->simvel );
	VectorCopy( cl.simorg, fd->simorg );

	VectorCopy( cl.viewheight, fd->viewheight );
	fd->idealpitch = cl.local.idealpitch;

	VectorCopy( cl.viewangles, fd->cl_viewangles );
	fd->health = cl.local.health;
	VectorCopy( cl.crosshairangle, fd->crosshairangle );
	if( Host_IsQuakeCompatible( ))
		fd->viewsize = scr_viewsize.value;
	else fd->viewsize = 120.0f;

	VectorCopy( cl.punchangle, fd->punchangle );
	fd->maxclients = cl.maxclients;
	fd->viewentity = cl.viewentity;
	fd->playernum = cl.playernum;
	fd->max_entities = clgame.maxEntities;
	fd->demoplayback = cls.demoplayback;
	fd->hardware = 1; // OpenGL

	if( cl.first_frame || cl.skip_interp )
	{
		cl.first_frame = false;		// now can be unlocked
		fd->smoothing = true;		// NOTE: currently this used to prevent ugly un-duck effect while level is changed
	}
	else fd->smoothing = cl.local.pushmsec;		// enable smoothing in multiplayer by server request (AMX uses)

	// get pointers to movement vars and user cmd
	fd->movevars = &clgame.movevars;
	fd->cmd = &cl.cmd;

	// setup viewport
	fd->viewport[0] = clgame.viewport[0];
	fd->viewport[1] = clgame.viewport[1];
	fd->viewport[2] = clgame.viewport[2];
	fd->viewport[3] = clgame.viewport[3];

	fd->onlyClientDraw = 0;	// reset clientdraw
	fd->nextView = 0;		// reset nextview
}

/*
===============
V_MergeOverviewRefdef

merge refdef with overview settings
===============
*/
static void V_RefApplyOverview( ref_viewpass_t *rvp )
{
	ref_overview_t	*ov = &clgame.overView;

	if( !CL_IsDevOverviewMode( ))
		return;

	// NOTE: Xash3D may use 16:9 or 16:10 aspects
	float aspect = (float)refState.width / (float)refState.height;

	float size_x = fabs( 8192.0f / ov->flZoom );
	float size_y = fabs( 8192.0f / (ov->flZoom * aspect ));

	// compute rectangle
	ov->xLeft = -(size_x / 2);
	ov->xRight = (size_x / 2);
	ov->yTop = -(size_y / 2);
	ov->yBottom = (size_y / 2);

	if( CL_IsDevOverviewMode() == 1 )
	{
		Con_NPrintf( 0, " Overview: Zoom %.2f, Map Origin (%.2f, %.2f, %.2f), Z Min %.2f, Z Max %.2f, Rotated %i\n",
		ov->flZoom, ov->origin[0], ov->origin[1], ov->origin[2], ov->zNear, ov->zFar, ov->rotated );
	}

	VectorCopy( ov->origin, rvp->vieworigin );
	rvp->vieworigin[2] = ov->zFar + ov->zNear;
	vec2_t mins = Vec2( rvp->vieworigin );
	vec2_t maxs = Vec2( rvp->vieworigin );

	mins[!ov->rotated] += ov->xLeft;
	maxs[!ov->rotated] += ov->xRight;
	mins[ov->rotated] += ov->yTop;
	maxs[ov->rotated] += ov->yBottom;

	rvp->viewangles[0] = 90.0f;
	rvp->viewangles[1] = 90.0f;
	rvp->viewangles[2] = (ov->rotated) ? (ov->flZoom < 0.0f) ? 180.0f : 0.0f : (ov->flZoom < 0.0f) ? -90.0f : 90.0f;

	SetBits( rvp->flags, RF_DRAW_OVERVIEW );

	ref.dllFuncs.GL_OrthoBounds( mins, maxs );
}

/*
====================
V_CalcFov
====================
*/
static float V_CalcFov( float *fov_x, float width, float height )
{
	if( *fov_x < 1.0f || *fov_x > 179.0f )
		*fov_x = 90.0f; // default value

	float x = width / tan( DEG2RAD( *fov_x ) * 0.5f );
	float half_fov_y = atan( height / x );

	return RAD2DEG( half_fov_y ) * 2;
}

/*
====================
V_AdjustFov
====================
*/
static void V_AdjustFov( float *fov_x, float *fov_y, float width, float height, qboolean lock_x )
{
	float x, y;

	if( width * 3 == 4 * height || width * 4 == height * 5 )
	{
		// 4:3 or 5:4 ratio
		return;
	}

	if( lock_x )
	{
		*fov_y = 2 * atan((width * 3) / (height * 4) * tan( *fov_y * M_PI_F / 360.0f * 0.5f )) * 360 / M_PI_F;
		return;
	}

	y = V_CalcFov( fov_x, 640, 480 );
	x = *fov_x;

	*fov_x = V_CalcFov( &y, height, width );
	if( *fov_x < x ) *fov_x = x;
	else *fov_y = y;
}

/*
====================
Cry of Fear world field of view (cof_fov)

Cry of Fear has no default_fov. Its client hard-codes the hip value 90 (an int
at VA 1018FEC0, read seven times, never written), 60 for its "zoom 2" state and
30 for the cinematic camera zoom, lerps gHUD.m_flFOV towards whichever of those
the server asked for through the player's vuser2[0] field, multiplies the
result by the archived cl_fovmultiplier in CHud::UpdateClientData and hands it
to the engine as client_data_t.fov. That is the only client -> engine FOV
channel, and it lands in cl.local.scr_fov. A slider bound straight to
cl_fovmultiplier therefore rescales ironsights and scripted cameras as well:
measured, the glock's ironsights base 50 becomes base 61 at multiplier 1.2222.

cof_fov is instead the player's preferred *hip* field of view, stored - like
the stock 90 and like Source's fov_desired - as the 4:3-equivalent base value.
FWGS still Hor+-corrects it below (r_adjust_fov), so base 90 keeps rendering as
106.26 degrees horizontal at 16:9.

The engine adds the difference to whatever the client asked for and tapers that
offset out as the request falls from the hip value towards the authored zoom
states, so a zoom keeps the framing its author chose:

	offset = bound( 70, cof_fov, 110 ) - COF_FOV_HIP
	knee   = bound( 30, cof_fov_zoom_knee, COF_FOV_HIP )
	t      = bound( 0, ( requested - knee ) / ( COF_FOV_HIP - knee ), 1 )
	result = requested + offset * t

With the default knee 60 - Cry of Fear's own "zoom 2" constant, and above every
ironsights FOV measured so far (the glock's is base 50) - a request of 90 gets
the whole offset, every request at or below 60 gets none at all, and the band
between them is a straight ramp so the client's own lerp into and out of a zoom
stays continuous. Set cof_fov_zoom_knee to 30 for the wider taper that also
moves ironsights by a third of the offset.

Only rvp->fov_x is touched. cl.local.scr_fov, the client's own m_flFOV and the
mouse sensitivity it derives from it, and everything that gets saved, are left
exactly as the game produced them.
====================
*/
#define COF_FOV_HIP	90.0f

static CVAR_DEFINE_AUTO( cof_fov, "90", FCVAR_ARCHIVE, "Cry of Fear world field of view at the hip, as the 4:3-equivalent base value the game itself uses (90 = stock). Widescreens are still Hor+-corrected by r_adjust_fov. Ironsights and scripted camera zooms keep their own framing, see cof_fov_zoom_knee" );
static CVAR_DEFINE_AUTO( cof_fov_zoom_knee, "60", 0, "field of view the cof_fov offset has fully tapered out at, so zoomed views keep the framing the game asked for. 60 is Cry of Fear's own \"zoom 2\" constant and sits above its ironsights values; 30 is its cinematic camera zoom" );

// registered from CL_InitLocal by the single line this patch adds to cl_main.c
void V_CoF_RegisterFovCvars( void )
{
	Cvar_RegisterVariable( &cof_fov );
	Cvar_RegisterVariable( &cof_fov_zoom_knee );
}

static float V_CoF_WorldFov( float requested )
{
	float	offset, knee, span, t;

	// gated on the Cry of Fear client, like the rest of the cof_* stack
	if( !GI || Q_stricmp( GI->gamefolder, "cryoffear" ))
		return requested;

	offset = bound( 70.0f, cof_fov.value, 110.0f ) - COF_FOV_HIP;
	if( offset == 0.0f )
		return requested; // stock: byte-identical output

	knee = bound( 30.0f, cof_fov_zoom_knee.value, COF_FOV_HIP );
	span = COF_FOV_HIP - knee;

	if( span < 0.1f )
		t = ( requested >= COF_FOV_HIP ) ? 1.0f : 0.0f;
	else t = bound( 0.0f, ( requested - knee ) / span, 1.0f );

	return requested + offset * t;
}

/*
=============
V_GetRefParams
=============
*/
static void V_GetRefParams( ref_params_t *fd, ref_viewpass_t *rvp )
{
	// part1: deniable updates
	VectorCopy( fd->simvel, cl.simvel );
	VectorCopy( fd->simorg, cl.simorg );
	VectorCopy( fd->punchangle, cl.punchangle );
	VectorCopy( fd->viewheight, cl.viewheight );

	// part2: really used updates
	VectorCopy( fd->crosshairangle, cl.crosshairangle );
	VectorCopy( fd->cl_viewangles, cl.viewangles );

	// setup ref_viewpass
	rvp->viewport[0] = fd->viewport[0];
	rvp->viewport[1] = fd->viewport[1];
	rvp->viewport[2] = fd->viewport[2];
	rvp->viewport[3] = fd->viewport[3];

	VectorCopy( fd->vieworg, rvp->vieworigin );
	VectorCopy( fd->viewangles, rvp->viewangles );

	rvp->viewentity = fd->viewentity;

	// calc FOV
	// the Cry of Fear world-FOV preference is applied here and here only, so
	// the client keeps the FOV it asked for (see V_CoF_WorldFov above)
	rvp->fov_x = bound( 10.0f, V_CoF_WorldFov( cl.local.scr_fov ), 150.0f ); // this is a final fov value

	// first we need to compute FOV and other things that needs for frustum properly work
	rvp->fov_y = V_CalcFov( &rvp->fov_x, clgame.viewport[2], clgame.viewport[3] );

	// adjust FOV for widescreen
	if( refState.wideScreen && r_adjust_fov.value )
		V_AdjustFov( &rvp->fov_x, &rvp->fov_y, clgame.viewport[2], clgame.viewport[3], false );

	rvp->flags = 0;

	if( fd->onlyClientDraw )
		SetBits( rvp->flags, RF_ONLY_CLIENTDRAW );
	SetBits( rvp->flags, RF_DRAW_WORLD );
}

/*
==================
V_PreRender

==================
*/
qboolean V_PreRender( void )
{
	// too early
	if( !ref.initialized )
		return false;

	if( host.status == HOST_SLEEP )
		return false;

	// if the screen is disabled (loading plaque is up)
	if( cls.disable_screen )
	{
		if(( host.realtime - cls.disable_screen ) > cl_timeout.value )
		{
			Con_Reportf( "%s: loading plaque timed out\n", __func__ );
			cls.disable_screen = 0.0f;
		}
		return false;
	}

	V_CheckGamma();

	ref.dllFuncs.R_BeginFrame( !cl.paused && ( cls.state == ca_active ));

	GL_UpdateSwapInterval( );

	return true;
}

//============================================================================

/*
====================
V_ApplyRefUnderwater

apply underwater fov effect
====================
*/
static void V_ApplyRefUnderwater( ref_viewpass_t *rvp )
{
	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP|RF_DRAW_OVERVIEW ))
		return;

	if( !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		return;

	if( cl.local.waterlevel < 3 )
		return;

	rvp->fov_x = atan( tan( DEG2RAD( rvp->fov_x ) / 2 ) * ( 0.97f + sin( cl.time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	rvp->fov_y = atan( tan( DEG2RAD( rvp->fov_y ) / 2 ) * ( 1.03f - sin( cl.time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);

}

/*
==================
V_RenderView

==================
*/
void V_RenderView( void )
{
	// HACKHACK: make ref params static
	// not really critical but allows client.dll to take address of refdef and don't trigger ASan
	static ref_params_t	rp;
	ref_viewpass_t	rvp;
	int		viewnum = 0;

	if( !cl.video_prepped || ( !ui_renderworld.value && UI_IsVisible() && !cl.background ))
		return; // still loading

	if( cl.world_generation != Mod_WorldGeneration( ))
		return; // the listen server loaded another world in place: not set up for drawing yet

	V_CalcViewRect ();	// compute viewport rectangle
	V_SetRefParams( &rp );
	V_SetupViewModel ();
	ref.dllFuncs.R_Set2DMode( false );
	SCR_DirtyScreen();
	ref.dllFuncs.GL_BackendStartFrame ();

	do
	{
		clgame.dllFuncs.pfnCalcRefdef( &rp );
		V_GetRefParams( &rp, &rvp );
		V_RefApplyOverview( &rvp );
		V_ApplyRefUnderwater( &rvp );

		if( viewnum == 0 && FBitSet( rvp.flags, RF_ONLY_CLIENTDRAW ))
		{
			ref.dllFuncs.R_ClearScreen();
		}

		GL_RenderFrame( &rvp );
		S_UpdateFrame( &rvp );
		viewnum++;

	} while( rp.nextView );

	// draw debug triangles on a server
	SV_DrawDebugTriangles ();
	ref.dllFuncs.GL_BackendEndFrame ();
}

#define POINT_SIZE		16.0f
#define NODE_INTERVAL_X(x)	(x * 16.0f)
#define NODE_INTERVAL_Y(x)	(x * 16.0f)

static void R_DrawLeafNode( float x, float y, float scale )
{
	float downScale = scale * 0.25f;// * POINT_SIZE;

	ref.dllFuncs.R_DrawStretchPic( x - downScale * 0.5f, y - downScale * 0.5f, downScale, downScale, 0, 0, 1, 1, R_GetBuiltinTexture( REF_PARTICLE_TEXTURE ) );
}

static void R_DrawNodeConnection( float x, float y, float x2, float y2 )
{
	ref.dllFuncs.Begin( TRI_LINES );
		ref.dllFuncs.Vertex3f( x, y, 0 );
		ref.dllFuncs.Vertex3f( x2, y2, 0 );
	ref.dllFuncs.End( );
}

static void R_ShowTree_r( mnode_t *node, float x, float y, float scale, int shownodes, mleaf_t *viewleaf )
{
	float	downScale = scale * 0.8f;

	downScale = Q_max( downScale, 1.0f );

	if( !node ) return;

	world.recursion_level++;

	if( node->contents < 0 )
	{
		mleaf_t	*leaf = (mleaf_t *)node;

		if( world.recursion_level > world.max_recursion )
			world.max_recursion = world.recursion_level;

		if( shownodes == 1 )
		{
			if( cl.worldmodel->leafs == leaf )
				ref.dllFuncs.Color4f( 1.0f, 1.0f, 1.0f, 1.0f );
			else if( viewleaf && viewleaf == leaf )
				ref.dllFuncs.Color4f( 1.0f, 0.0f, 0.0f, 1.0f );
			else ref.dllFuncs.Color4f( 0.0f, 1.0f, 0.0f, 1.0f );
			R_DrawLeafNode( x, y, scale );
		}
		world.recursion_level--;
		return;
	}

	if( shownodes == 1 )
	{
		ref.dllFuncs.Color4f( 0.0f, 0.0f, 1.0f, 1.0f );
		R_DrawLeafNode( x, y, scale );
	}
	else if( shownodes == 2 )
	{
		R_DrawNodeConnection( x, y, x - scale, y + scale );
		R_DrawNodeConnection( x, y, x + scale, y + scale );
	}

	R_ShowTree_r( node_child( node, 1, cl.worldmodel ), x - scale, y + scale, downScale, shownodes, viewleaf );
	R_ShowTree_r( node_child( node, 0, cl.worldmodel ), x + scale, y + scale, downScale, shownodes, viewleaf );

	world.recursion_level--;
}

static void R_ShowTree( void )
{
	float	x = (float)((refState.width - (int)POINT_SIZE) >> 1);
	float	y = NODE_INTERVAL_Y(1.0f);

	if( !cl.worldmodel || !r_showtree.value )
		return;

	world.recursion_level = 0;
	mleaf_t *viewleaf = Mod_PointInLeaf( refState.vieworg, cl.worldmodel->nodes, cl.worldmodel );

	ref.dllFuncs.TriRenderMode( kRenderTransTexture );

	//pglLineWidth( 2.0f );
	ref.dllFuncs.Color4f( 1, 0.7f, 0, 1.0f );
	//pglDisable( GL_TEXTURE_2D );
	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 2, viewleaf );
	//pglEnable( GL_TEXTURE_2D );
	//pglLineWidth( 1.0f );

	R_ShowTree_r( cl.worldmodel->nodes, x, y, world.max_recursion * 3.5f, 1, viewleaf );

	Con_NPrintf( 0, "max recursion %d\n", world.max_recursion );
}

/*
==================
V_PostRender

==================
*/
void V_PostRender( void )
{
	qboolean		draw_2d = false;

	ref.dllFuncs.R_AllowFog( false );
	ref.dllFuncs.R_Set2DMode( true );

	if( cls.state == ca_active && cls.signon == SIGNONS && cls.scrshot_action != scrshot_mapshot )
	{
		SCR_TileClear();
		CL_DrawHUD( CL_ACTIVE );
		if( !cof_skip_vgui_paint.value )
			VGui_Paint();
	}

	switch( cls.scrshot_action )
	{
	case scrshot_inactive:
	case scrshot_normal:
	case scrshot_snapshot:
		draw_2d = true;
		break;
	}

	if( draw_2d )
	{
		SCR_RSpeeds();
		SCR_NetSpeeds();
		SCR_DrawPos();
		SCR_DrawEnts();
		SCR_DrawNetGraph();
		SCR_DrawUserCmd();
		Joy_DrawDebug();
		IN_GyroDrawDebug();
		SV_DrawOrthoTriangles();
		CL_DrawDemoRecording();
		CL_DrawHUD( CL_CHANGELEVEL );
		ref.dllFuncs.R_ShowTextures();
		R_ShowTree();
		UI_UpdateMenu( host.realtime );
		Con_DrawVersion();
		Con_DrawConsole();
		Con_DrawDebug(); // must be last
		Touch_Draw();
		OSK_Draw();

		S_ExtraUpdate();
	}

	SCR_MakeScreenShot();
	ref.dllFuncs.R_AllowFog( true );
	Platform_SetTimer( 0.0f );
	ref.dllFuncs.R_EndFrame();

	V_CheckGammaEnd();
}
