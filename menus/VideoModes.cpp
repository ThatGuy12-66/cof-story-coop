/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Framework.h"
#include "MenuStrings.h"
#include "Table.h"
#include "CheckBox.h"
#include "Slider.h"
#include "YesNoMessageBox.h"
#include "SpinControl.h"
#include "SelectionPopup.h"
#include "utlvector.h"
#include "StringArrayModel.h"
#include "CryOfFear.h"
#include "Theme.h"

#define ART_BANNER		"gfx/shell/head_vidmodes"
#define ART_GAMMA		"gfx/shell/gamma"

// TODO:
// - Get rid of vid mode strings, always compare the Width X Height of the window
//   Because if user has resized the window, or set it to cmdline, it will go
//   out of sync with the vid mode strings list
//   Doing this, however, will require API extension

class CMenuVidModesModel : public CMenuBaseArrayModel
{
public:
	void Update() override;
	int GetRows() const override
	{
		return m_szModes.Count();
	}

	const char *GetText( int line ) override
	{
		return m_szModes[line];
	}

private:
	CUtlVector<const char *> m_szModes;
};

class CMenuRenderersModel : public CMenuBaseArrayModel
{
public:
	void Update() override;
	int GetRows() const override
	{
		return m_refs.Count();
	}

	const char *GetText( int i ) override
	{
		return m_refs[i].readable;
	}

	const char *GetShortName( int i )
	{
		return m_refs[i].shortName;
	}

	void Add( const char *str )
	{
		refdll temp;

		Q_strncpy( temp.shortName, str, sizeof( temp.shortName ));
		Q_strncpy( temp.readable, str, sizeof( temp.readable ));

		m_refs.AddToTail( temp );
	}
private:
	struct refdll
	{
		char shortName[64];
		char readable[64];
	};

	CUtlVector<refdll> m_refs;
};

/*
=================
CMenuReadoutSlider

A themed slider that prints its own current value at the right end of the label
line the base class already draws, because the two view sliders are the first
controls on this page whose *number* is the point - "Gamma 0.62" means nothing,
"Field of view 90" means everything.

Only the readout is added; the track, the knob, the cursor mapping and the key
handling are CMenuSlider's, untouched. Without the theme the class behaves
exactly like its base (the readout is drawn only in theme mode), so the WON
path is unaffected.

szFollowText, when set, replaces the number at flFollowValue - that is how
"Viewmodel FOV" spells its zero: the slider's lowest step is one below the
useful range and reads "Follow FOV" instead of "54".
=================
*/
class CMenuReadoutSlider : public CMenuSlider
{
public:
	CMenuReadoutSlider() : CMenuSlider(), szFollowText( NULL ), flFollowValue( 0.0f ) { }

	void Draw() override
	{
		CMenuSlider::Draw();

		if( !UI_ThemeActive( ))
			return;

		char text[64];
		float value = GetCurrentValue();

		if( szFollowText && value <= flFollowValue )
			Q_strncpy( text, szFollowText, sizeof( text ));
		else
			snprintf( text, sizeof( text ), "%i", (int)( value + 0.5f ));

		// the base draws szName at exactly this baseline, left-aligned
		UI_DrawString( font, m_scPos.x, m_scPos.y - (int)( m_scChSize * 1.5f ),
			m_scSize.w, m_scChSize, text,
			FBitSet( iFlags, QMF_GRAYED ) ? THEME_DISABLED : THEME_TEXT, m_scChSize,
			QM_TOPRIGHT, ETF_FORCECOL | ETF_NO_WRAP );
	}

	const char *szFollowText;	// drawn instead of the number at flFollowValue
	float       flFollowValue;
};

// Keep the resolution table's selected-index/apply plumbing and opt-out path.
class CMenuResolutionSelection : public CMenuTable
{
public:
    CMenuResolutionSelection() : model(NULL) {}
    void SetModel(CMenuBaseArrayModel *m) { model=m;CMenuTable::SetModel(m); }
    void VidInit() override
    {
        if(UI_CoFSelectionLists())CMenuBaseItem::VidInit();
        else CMenuTable::VidInit();
    }
    bool MouseMove(int x,int y) override { return UI_CoFSelectionLists()?true:CMenuTable::MouseMove(x,y); }
    bool KeyDown(int key) override
    {
        if(!UI_CoFSelectionLists())return CMenuTable::KeyDown(key);
        return UI::Key::IsEnter(key)||UI::Key::IsLeftMouse(key);
    }
    bool KeyUp(int key) override
    {
        if(!UI_CoFSelectionLists())return CMenuTable::KeyUp(key);
        if(model && (UI::Key::IsEnter(key)||(UI::Key::IsLeftMouse(key)&&UI_CursorInRect(m_scPos,m_scSize))))
        { UI_CoFOpenSelection(this,model->GetRows(),GetCurrentIndex(),Text,Commit);return true; }
        return false;
    }
    void Draw() override
    {
        if(!UI_CoFSelectionLists()){CMenuTable::Draw();return;}
        char text[512];Text(this,GetCurrentIndex(),text,sizeof(text));
        szName=L("GameUI_Resolution");UI_CoFDrawSelection(this,text);
    }
private:
    static void Text(CMenuBaseItem *base,int row,char *text,int size)
    {
        CMenuResolutionSelection *s=static_cast<CMenuResolutionSelection*>(base);
        Q_strncpy(text,s->model&&row>=0&&row<s->model->GetRows()?s->model->GetText(row):"",size);
    }
    static void Commit(CMenuBaseItem *base,int row)
    {
        CMenuResolutionSelection *s=static_cast<CMenuResolutionSelection*>(base);
        if(s->model&&row>=0&&row<s->model->GetRows())s->SetCurrentIndex(row);
    }
    CMenuBaseArrayModel *model;
};

class CMenuVidModes : public CMenuFramework
{
private:
	void _Init() override;
	void _VidInit() override;
	void Reload() override;
	void Draw() override; // put test mode timer here

public:
	CMenuVidModes() : CMenuFramework( "CMenuVidModes" ), restartMsgBox( true ),
		hTestImage( 0 ), testModeTimer( 0 ) { }

	void SetMode( int mode );
	void SetMode( int w, int h );
	void SetConfig();
	void RevertChanges();
	void ApplyChanges();
	void FinalizeChanges();

	void GetConfig();

	void GetRendererConfig();
	void WriteRendererConfig();
	bool IsRendererChanged();

	CMenuCheckBox	vsync;

	CMenuResolutionSelection	vidList;
	CMenuVidModesModel vidListModel;

	CMenuYesNoMessageBox testModeMsgBox;
	CMenuYesNoMessageBox restartMsgBox;

	CMenuRenderersModel renderersModel;
	CMenuSpinControl renderers;
	CMenuSpinControl windowMode;

	// ------------------------------------------------------------------
	// theme mode only: the former "Video options" page, merged in so there
	// is one Video page instead of a hub with two children. The upstream
	// CMenuVidOptions still exists and still owns the WON path.
	// ------------------------------------------------------------------
	void InitVideoOptions();
	void LayoutVideoOptions( const Rect &col );
	void ReloadVideoOptions();
	void WriteVideoOptions();
	void UpdateGamma();
	void GetGamma();

	// Cry of Fear only: the two view sliders, bound to the engine's cof_fov
	// (xash.dll, V_GetRefParams) and the renderer's cof_viewmodel_fov
	// (ref_gl.dll, R_DrawViewModel). Both apply live, on drag.
	void UpdateFov();
	void GetFov();
	void UpdateViewmodelFov();
	void GetViewmodelFov();

	CMenuReadoutSlider fov;
	CMenuReadoutSlider viewmodelFov;

	CMenuSlider	gammaIntensity;
	CMenuSlider	brightness;
	CMenuSlider	contrast;	// Cry of Fear only, gl_contrast
	CMenuCheckBox	vbo;
	CMenuCheckBox	swwater;
	CMenuCheckBox	overbright;
	CMenuCheckBox	filtering;
	CMenuCheckBox	detailtex;
	CMenuCheckBox	hudscale;
	CMenuYesNoMessageBox hudScaleMsgBox;
	HIMAGE		hTestImage;

	// Cry of Fear renders through its own Paranoia OpenGL path and honours its
	// own cvars, so several of the boxes above do nothing in this game and are
	// not shown; see the options audit in docs/cof-ui-m3-theme.md.
	bool		m_bCoF;

	int prevMode;
	int prevModeX;
	int prevModeY;
	int prevFullscreen;
	float testModeTimer;
	char testModeMsg[256];
};

void CMenuVidModesModel::Update()
{
	m_szModes.Purge();

	for( int i = 0; ; i++ )
	{
		const char *mode = EngFuncs::GetModeString( i );

		if( !mode )
			break;

		m_szModes.AddToTail( mode );
	}
}

void CMenuRenderersModel::Update()
{
	const char *r_refdll_loaded = EngFuncs::GetCvarString( "r_refdll_loaded" );
	m_refs.Purge();

	for( int i = 0; ; i++ )
	{
		refdll temp;

		if( !EngFuncs::GetRenderers( i, temp.shortName, sizeof( temp.shortName ), temp.readable, sizeof( temp.readable )))
			break;

		// append asterisk to currently loaded renderer
		if( !stricmp( r_refdll_loaded, temp.shortName ))
			strncat( temp.readable, "*", sizeof( temp.readable ) - strlen( temp.readable ) - 1 );

		m_refs.AddToTail( temp );
	}
}

void CMenuVidModes::GetRendererConfig()
{
	// get current _configured_ renderer
	const char *refdll = EngFuncs::GetCvarString( "r_refdll" );

	if( !refdll[0] )
	{
		renderers.SetCurrentValue( 0.0f );
		return;
	}

	int i;
	for( i = 0; i < renderersModel.GetRows(); i++ )
	{
		if( !stricmp( renderersModel.GetShortName( i ), refdll ))
		{
			renderers.SetCurrentValue( i );
			break;
		}
	}

	if( i == renderersModel.GetRows( ))
	{
		renderersModel.Add( refdll ); // add unknown user selection
		renderers.SetCurrentValue( i );
	}
}

void CMenuVidModes::WriteRendererConfig()
{
	int i = renderers.GetCurrentValue();
	EngFuncs::CvarSetString( "r_refdll", renderersModel.GetShortName( i ));
}

// the renderer library is only loaded on startup
bool CMenuVidModes::IsRendererChanged()
{
	int i = renderers.GetCurrentValue();

	if( i < 0 || i >= renderersModel.GetRows( ))
		return false;

	return stricmp( renderersModel.GetShortName( i ), EngFuncs::GetCvarString( "r_refdll_loaded" )) != 0;
}

/*
=================
CMenuVidModes, the merged video-options half

Feedback round: "Video modes" and "Video options" were two children of a Video
hub, which is three clicks for one screen. They are one page now - modes and
the resolution list on the left, image settings on the right, one button row -
and Options > Video opens it directly (menus/Video.cpp). Everything below is
the CMenuVidOptions behaviour, unchanged in substance: the same cvars, the same
hud_scale warning, the same per-renderer graying.
=================
*/
/*
=================
The Cry of Fear image cvars

Measured in the fixture (stage1/ui-m1-engine-fixture-20260921, the vq-* runs):
with the same save, the same camera and the same frame count, "gamma" 1.8 -> 3.0
and "brightness" 0 -> 3 change nothing at all, while the client's own
"gl_gamma", "gl_brightness" and "gl_contrast" change most of the frame. That is
what the binary says too: client.dll registers gl_gamma "1", gl_brightness "0"
and gl_contrast "1" itself and applies them inside its Paranoia lightmap path,
and it never reads the engine's gamma or brightness. It also clamps its own two:
brightness above 0.3 and contrast above 2.0 are written straight back down (at
client.dll VA 1005A195 and 1005A1C3), so those are the real ends of the
sliders.

So for this game the two sliders drive the cvars that work, a third is added for
contrast because the client treats the three as one group, and the engine's
gamma test image is not touched - it previews an engine-side transform the
Paranoia path does not perform.
=================
*/
#define COF_GAMMA_MIN      0.4f
#define COF_GAMMA_MAX      2.0f
#define COF_BRIGHTNESS_MIN 0.0f
#define COF_BRIGHTNESS_MAX 0.3f
#define COF_CONTRAST_MIN   0.2f
#define COF_CONTRAST_MAX   2.0f

/*
=================
The Cry of Fear view cvars

Both are added by the engine side of the FOV round
(stage1/fov-investigation-20260922/RESULTS.md, stage1/fov-impl-20260922):

  cof_fov            xash.dll,   FCVAR_ARCHIVE,  default 90, 70..110.
                     Remapped in V_GetRefParams on rvp->fov_x only, tapering
                     out towards the cinematic zoom, so ironsights and scripted
                     cameras keep their authored FOV. The value is the 4:3 base
                     (Source's fov_desired convention); FWGS Hor+-corrects it,
                     90 -> 106.26 degrees at 16:9.
  cof_viewmodel_fov  ref_gl.dll, FCVAR_GLCONFIG, default 0, 55..90.
                     0 means "follow the world FOV", which is the stock look.

Nothing is registered here. The menu library has no cvar of its own for either:
it reads them by name and writes them by name, the way every other page does.
A build whose engine or renderer does not carry the patch is handled instead of
misreported - EngFuncs::GetCvarString returns an empty string for a name the
engine never registered, which is the one reliable existence test, and the
slider then shows the documented default rather than a bogus 0. Writing to a
name that does not exist is harmless: Cvar_Set logs "variable not found" and
returns (engine/common/cvar.c).

The sliders apply live, on drag, like the gamma group above them - there is no
restart and no Apply step, because both cvars are read per frame by the code
that uses them.
=================
*/
#define COF_FOV_MIN        70
#define COF_FOV_MAX        110
#define COF_FOV_DEFAULT    90
#define COF_VMFOV_MIN      55
#define COF_VMFOV_MAX      90
// one step below the useful range, drawn as "Follow FOV" and written as 0.
// A slider cannot hold a gap, so the zero lives at the bottom of the track
// instead of in the middle of it.
#define COF_VMFOV_FOLLOW   ( COF_VMFOV_MIN - 1 )

// Reads a cvar that a not-yet-patched engine or renderer may not have.
static float UI_CoFViewCvar( const char *name, float defValue )
{
	const char *str = EngFuncs::GetCvarString( name );

	if( !str || !str[0] )
		return defValue;

	return EngFuncs::GetCvarFloat( name );
}

void CMenuVidModes::GetFov()
{
	float value = UI_CoFViewCvar( "cof_fov", COF_FOV_DEFAULT );

	// anything the slider cannot represent - the engine cvar's own 0 "off"
	// value, or a hand-edited config - reads as the shipped default, which is
	// the neutral setting the remap does nothing at.
	if( value < COF_FOV_MIN || value > COF_FOV_MAX )
		value = COF_FOV_DEFAULT;

	fov.SetCurrentValue( value );
	fov.SetOriginalValue( value );
}

void CMenuVidModes::UpdateFov()
{
	EngFuncs::CvarSetValue( "cof_fov", (float)(int)( fov.GetCurrentValue() + 0.5f ));
}

void CMenuVidModes::GetViewmodelFov()
{
	float value = UI_CoFViewCvar( "cof_viewmodel_fov", 0.0f );

	// 0 (follow the world FOV) and anything out of range sit on the bottom step
	if( value < COF_VMFOV_MIN || value > COF_VMFOV_MAX )
		value = COF_VMFOV_FOLLOW;

	viewmodelFov.SetCurrentValue( value );
	viewmodelFov.SetOriginalValue( value );
}

void CMenuVidModes::UpdateViewmodelFov()
{
	float value = viewmodelFov.GetCurrentValue();

	EngFuncs::CvarSetValue( "cof_viewmodel_fov",
		value <= COF_VMFOV_FOLLOW ? 0.0f : (float)(int)( value + 0.5f ));
}

void CMenuVidModes::UpdateGamma()
{
	if( m_bCoF )
	{
		EngFuncs::CvarSetValue( "gl_gamma",
			RemapVal( gammaIntensity.GetCurrentValue(), 0.0f, 1.0f, COF_GAMMA_MIN, COF_GAMMA_MAX ));
		EngFuncs::CvarSetValue( "gl_brightness",
			RemapVal( brightness.GetCurrentValue(), 0.0f, 1.0f, COF_BRIGHTNESS_MIN, COF_BRIGHTNESS_MAX ));
		EngFuncs::CvarSetValue( "gl_contrast",
			RemapVal( contrast.GetCurrentValue(), 0.0f, 1.0f, COF_CONTRAST_MIN, COF_CONTRAST_MAX ));
		return;
	}

	float val1 = RemapVal( gammaIntensity.GetCurrentValue(), 0.0, 1.0, 1.8, 3.0 );
	float val2 = RemapVal( brightness.GetCurrentValue(), 0.0, 1.0, 0.0, 3.0 );

	EngFuncs::CvarSetValue( "gamma", val1 );
	EngFuncs::CvarSetValue( "brightness", val2 );

	if( hTestImage )
		EngFuncs::ProcessImage( hTestImage, val1, val2 );
}

void CMenuVidModes::GetGamma()
{
	if( m_bCoF )
	{
		float g = EngFuncs::GetCvarFloat( "gl_gamma" );
		float b = EngFuncs::GetCvarFloat( "gl_brightness" );
		float c = EngFuncs::GetCvarFloat( "gl_contrast" );

		gammaIntensity.SetCurrentValue( RemapVal( g, COF_GAMMA_MIN, COF_GAMMA_MAX, 0.0f, 1.0f ));
		brightness.SetCurrentValue( RemapVal( b, COF_BRIGHTNESS_MIN, COF_BRIGHTNESS_MAX, 0.0f, 1.0f ));
		contrast.SetCurrentValue( RemapVal( c, COF_CONTRAST_MIN, COF_CONTRAST_MAX, 0.0f, 1.0f ));

		gammaIntensity.SetOriginalValue( g );
		brightness.SetOriginalValue( b );
		contrast.SetOriginalValue( c );
		return;
	}

	float val1 = EngFuncs::GetCvarFloat( "gamma" );
	float val2 = EngFuncs::GetCvarFloat( "brightness" );

	gammaIntensity.SetCurrentValue( RemapVal( val1, 1.8f, 3.0f, 0.0f, 1.0f ));
	brightness.SetCurrentValue( RemapVal( val2, 0.0f, 3.0f, 0.0f, 1.0f ));

	if( hTestImage )
		EngFuncs::ProcessImage( hTestImage, val1, val2 );

	gammaIntensity.SetOriginalValue( val1 );
	brightness.SetOriginalValue( val2 );
}

void CMenuVidModes::WriteVideoOptions()
{
	// gamma, brightness and contrast are written as they are dragged.
	// A box that is not on the page for this game is not written either: it
	// was never shown, so it has no value the user chose.
	if( detailtex.IsVisible( ))  detailtex.WriteCvar();
	if( vbo.IsVisible( ))        vbo.WriteCvar();
	if( swwater.IsVisible( ))    swwater.WriteCvar();
	if( overbright.IsVisible( )) overbright.WriteCvar();
	if( filtering.IsVisible( ))  filtering.WriteCvar();
	if( hudscale.IsVisible( ))   hudscale.WriteCvar();
}

void CMenuVidModes::InitVideoOptions()
{
	hTestImage = EngFuncs::PIC_Load( ART_GAMMA, PIC_KEEP_SOURCE | PIC_EXPAND_SOURCE );

	gammaIntensity.szName = L( "GameUI_Gamma" );
	gammaIntensity.Setup( 0.0, 1.0, 0.025 );
	gammaIntensity.onChanged = VoidCb( &CMenuVidModes::UpdateGamma );
	gammaIntensity.onCvarGet = VoidCb( &CMenuVidModes::GetGamma );

	brightness.szName = L( "GameUI_Brightness" );
	brightness.Setup( 0, 1.0, 0.025 );
	brightness.onChanged = VoidCb( &CMenuVidModes::UpdateGamma );
	brightness.onCvarGet = VoidCb( &CMenuVidModes::GetGamma );

	contrast.szName = L( "Contrast" );
	contrast.Setup( 0, 1.0, 0.025 );
	contrast.onChanged = VoidCb( &CMenuVidModes::UpdateGamma );
	contrast.onCvarGet = VoidCb( &CMenuVidModes::GetGamma );

	fov.szName = L( "Field of view" );
	fov.Setup( COF_FOV_MIN, COF_FOV_MAX, 1.0f );
	fov.onChanged = VoidCb( &CMenuVidModes::UpdateFov );
	fov.onCvarGet = VoidCb( &CMenuVidModes::GetFov );
	fov.szStatusText = L( "The 4:3 base field of view; 90 is Cry of Fear's own. Ironsights and scripted cameras are not rescaled." );

	viewmodelFov.szName = L( "Viewmodel FOV" );
	viewmodelFov.Setup( COF_VMFOV_FOLLOW, COF_VMFOV_MAX, 1.0f );
	viewmodelFov.szFollowText = L( "Follow FOV" );
	viewmodelFov.flFollowValue = COF_VMFOV_FOLLOW;
	viewmodelFov.onChanged = VoidCb( &CMenuVidModes::UpdateViewmodelFov );
	viewmodelFov.onCvarGet = VoidCb( &CMenuVidModes::GetViewmodelFov );
	viewmodelFov.szStatusText = L( "Field of view of the held weapon only. At the lowest step it follows the world field of view, which is the stock look." );

	detailtex.szName = L( "Detail textures" );
	vbo.szName = L( "Use VBO" );
	swwater.szName = L( "Water ripples" );
	overbright.szName = L( "Overbrights" );
	filtering.szName = L( "Texture filtering" );
	hudscale.szName = L( "Auto scale HUD" );

	hudScaleMsgBox.SetMessage( L( "^1WARNING: This is an experimental option and turning it on might break mods!^7\n\nReload the game or reconnect to the server to apply the settings."));
	hudScaleMsgBox.onNegative.pExtra = &hudscale;
	SET_EVENT_MULTI( hudScaleMsgBox.onNegative,
	{
		CMenuCheckBox *cb = (CMenuCheckBox *)pExtra;

		cb->bChecked = false;
		cb->SetCvarValue( 0.0f );
	});
	hudScaleMsgBox.Link( this );

	SET_EVENT_MULTI( hudscale.onChanged,
	{
		CMenuVidModes *parent = (CMenuVidModes *)pSelf->Parent();

		if( EngFuncs::ClientInGame( ))
			parent->hudScaleMsgBox.Show();
	});

	SET_EVENT_MULTI( hudscale.onCvarWrite,
	{
		CMenuCheckBox *cb = (CMenuCheckBox *)pSelf;

		// scale the HUD as if the screen were 1024x768, keeping the aspect
		if( cb->bChecked )
			EngFuncs::CvarSetValue( cb->CvarName(), 1024.0f );
		else
			EngFuncs::CvarSetValue( cb->CvarName(), 0.0f );
	});

	SET_EVENT_MULTI( hudscale.onCvarGet,
	{
		CMenuCheckBox *cb = (CMenuCheckBox *)pSelf;

		cb->bChecked = EngFuncs::GetCvarFloat( cb->CvarName() ) >= 640.0f;
	});

	AddItem( gammaIntensity );
	AddItem( brightness );
	AddItem( contrast );
	AddItem( detailtex );
	AddItem( vbo );
	AddItem( swwater );
	AddItem( overbright );
	AddItem( filtering );
	AddItem( hudscale );
	// last, so the keyboard cursor reaches them in the order they are drawn
	AddItem( fov );
	AddItem( viewmodelFov );

	// Options audit, 2026-09-22 (docs/cof-ui-m3-theme.md). Every one of these
	// was measured in the fixture: same save, same camera, same frame count,
	// the cvar at two extremes, the two screenshots compared pixel by pixel on
	// a surface whose same-settings noise floor is exactly zero. What Cry of
	// Fear does not use is taken off its page rather than left there doing
	// nothing.
	//   r_detailtextures  0 -> 1   at the floor, and the game ships no
	//                             maps/<map>_detail.txt at all; its own detail
	//                             textures are the client's gl_detailtex,
	//                             which measured at the floor too. Removed,
	//                             and defaulted to 0 in UI_ThemeApplyDeferredDefaults.
	//   gl_overbright     0 -> 1   at the floor: Cry of Fear loads its own
	//                             "additional lightmaps" in the Paranoia path
	//                             and lights the world there, so the engine's
	//                             overbright never reaches what you see.
	//                             Removed.
	// Kept, measured well above the floor: gl_texture_nearest (20%) and
	// hud_scale (50%). Kept without a verdict: gl_vbo (a performance option,
	// not a visual one) and r_ripple (the test scene has no water in frame).
	contrast.SetVisibility( m_bCoF );
	detailtex.SetVisibility( !m_bCoF );
	overbright.SetVisibility( !m_bCoF );
	// hud_scale is removed for Cry of Fear: the client builds its own
	// renderer's viewport and projection out of the same screen size the
	// engine reports for the HUD, so a virtual screen zooms and shifts the 3D
	// view (measured, docs/cof-ui-m4-scaling.md). The engine ignores the cvar
	// for this game entirely; the supported control is HUD scale on the Game
	// options page, which drives cof_ui_scale / cof_ui_scale_user instead.
	hudscale.SetVisibility( !m_bCoF );

	// The two view sliders are Cry of Fear's: cof_fov is remapped by the
	// engine only for this game's client, and cof_viewmodel_fov overrides a
	// projection the stock renderer has no reason to change. They sit under
	// the image settings in the right column rather than in a column of their
	// own - the page is 940x620 with 494 units of content height, the image
	// block ends at 318, and two more sliders fit in the remainder with room
	// to spare at every resolution down to 1280x720.
	fov.SetVisibility( m_bCoF );
	viewmodelFov.SetVisibility( m_bCoF );

	gammaIntensity.LinkCvar( m_bCoF ? "gl_gamma" : "gamma" );
	brightness.LinkCvar( m_bCoF ? "gl_brightness" : "brightness" );
	contrast.LinkCvar( "gl_contrast" );
	detailtex.LinkCvar( "r_detailtextures" );
	swwater.LinkCvar( "r_ripple" );
	vbo.LinkCvar( "gl_vbo" );
	overbright.LinkCvar( "gl_overbright" );
	// filtering is linked in ReloadVideoOptions: the cvar name differs between
	// ref_gl and ref_soft
	hudscale.LinkCvar( "hud_scale" );

	// LinkCvar runs onCvarGet, which is where the missing-cvar fallback lives
	fov.LinkCvar( "cof_fov" );
	viewmodelFov.LinkCvar( "cof_viewmodel_fov" );
}

void CMenuVidModes::LayoutVideoOptions( const Rect &col )
{
	int y = col.pt.y + THEME_LABEL_TALL + 8;

	CMenuSlider *sliders[] = { &gammaIntensity, &brightness, &contrast };

	for( int i = 0; i < 3; i++ )
	{
		if( !sliders[i]->IsVisible( ))
			continue;

		sliders[i]->SetRect( col.pt.x, y, col.sz.w, THEME_SLIDER_H );
		y += THEME_CTRL_PITCH;
	}

	y += 6;

	CMenuCheckBox *boxes[] = { &detailtex, &vbo, &swwater, &overbright, &filtering, &hudscale };

	// the removed boxes leave no gap: only what is on the page is placed
	for( int i = 0; i < 6; i++ )
	{
		if( !boxes[i]->IsVisible( ))
			continue;

		boxes[i]->SetCoord( col.pt.x, y );
		y += THEME_ROW_PITCH;
	}

	// The view sliders close the column. The first one pays for its own label
	// line (THEME_CTRL_PITCH only covers the gap *between* stacked sliders),
	// plus a visual break from the checkbox block above it.
	CMenuSlider *view[] = { &fov, &viewmodelFov };
	bool firstView = true;

	for( int i = 0; i < 2; i++ )
	{
		if( !view[i]->IsVisible( ))
			continue;

		if( firstView )
		{
			y += 14 + THEME_LABEL_TALL + 8;
			firstView = false;
		}

		view[i]->SetRect( col.pt.x, y, col.sz.w, THEME_SLIDER_H );
		y += THEME_CTRL_PITCH;
	}
}

void CMenuVidModes::ReloadVideoOptions()
{
	bool gl_active = !strnicmp( EngFuncs::GetCvarString( "r_refdll_loaded" ), "gl", 2 );
	bool soft_active = !stricmp( EngFuncs::GetCvarString( "r_refdll_loaded" ), "soft" );

	detailtex.SetGrayed( !gl_active );
	detailtex.SetInactive( !gl_active );

	vbo.SetGrayed( !gl_active );
	vbo.SetInactive( !gl_active );

	if( gl_active )
	{
		if( EngFuncs::textfuncs.pfnIsCvarReadOnly( "gl_vbo" ) > 0 )
		{
			SET_EVENT_MULTI( vbo.onCvarChange,
			{
				CMenuCheckBox *cb = (CMenuCheckBox *)pSelf;
				cb->bChecked = false;
				UI_ShowMessageBox( L( "Not supported on your GPU" ));
			});

			vbo.onCvarWrite = CEventCallback::NoopCb;
		}
		else
		{
			vbo.onCvarWrite.Reset();
			vbo.onCvarChange.Reset();
		}
	}

	swwater.SetGrayed( !gl_active );
	swwater.SetInactive( !gl_active );

	overbright.SetGrayed( !gl_active );
	overbright.SetInactive( !gl_active );

	if( soft_active || gl_active )
	{
		filtering.SetGrayed( false );
		filtering.SetInactive( false );

		if( soft_active )
		{
			// no need to invert, 0 disables filtering
			filtering.LinkCvar( "sw_texfilt" );
		}
		else
		{
			SET_EVENT_MULTI( filtering.onCvarGet,
			{
				CMenuCheckBox *cb = (CMenuCheckBox *)pSelf;

				if( EngFuncs::GetCvarFloat( cb->CvarName( )))
					cb->bChecked = false;
				else cb->bChecked = true;
			});
			SET_EVENT_MULTI( filtering.onCvarWrite,
			{
				CMenuCheckBox *cb = (CMenuCheckBox *)pSelf;
				if( cb->bChecked )
					EngFuncs::CvarSetValue( cb->CvarName(), 0.0f );
				else EngFuncs::CvarSetValue( cb->CvarName(), 1.0f );
			});

			filtering.LinkCvar( "gl_texture_nearest" );
		}
	}
	else
	{
		filtering.SetGrayed( true );
		filtering.SetInactive( true );
	}
}

void CMenuVidModes::GetConfig()
{
	float fullscreen = EngFuncs::GetCvarFloat( "fullscreen" );
	fullscreen = bound( 0, fullscreen, 2 ); // windowed, fullscreen, borderless

	float vid_mode = EngFuncs::GetCvarFloat( "vid_mode" );
	vid_mode = bound( 0, vid_mode, vidListModel.GetRows( ));

	windowMode.SetCurrentValue( fullscreen );
	vidList.SetCurrentIndex( vid_mode );

	renderers.UpdateCvar( true );
	vsync.UpdateCvar( true );

	ApplyChanges();
}

void CMenuVidModes::SetMode( int w, int h )
{
	// only possible on Xash3D FWGS!
	EngFuncs::ClientCmdF( true, "vid_setmode %i %i\n", w, h );
}

void CMenuVidModes::SetMode( int mode )
{
	// vid_setmode is a new command, which does not depends on
	// static resolution list but instead uses dynamic resolution
	// list provided by video backend
	EngFuncs::ClientCmdF( true, "vid_setmode %i\n", mode );
}

/*
=================
UI_VidModes_SetConfig
=================
*/
void CMenuVidModes::SetConfig( )
{
	bool testMode = false;
	int  currentWindowModeIndex = windowMode.GetCurrentValue();
	int  currentModeIndex = vidList.GetCurrentIndex();
	bool isVidModeChanged = prevMode != currentModeIndex;
	bool isWindowedModeChanged = prevFullscreen != currentWindowModeIndex;

	/*
	checking windowed mode first because it'll be checked next in
	screen resolution changing code, otherwise when user try to
	change screen resolution and windowed flag at same time,
	only resolution will be changed.
	*/

	if( isWindowedModeChanged ) // moved to fullscreen, enable test mode
		testMode |= currentWindowModeIndex > 0;

	if( isVidModeChanged ) // have changed resolution, but enable test mode only in fullscreen
		testMode |= currentWindowModeIndex > 0;

	if( testMode ) // show this dialog before changing any settings
	{
		testModeMsgBox.Show();
		testModeTimer = gpGlobals->time + 10.0f; // ten seconds should be enough
	}

	vsync.WriteCvar();
	renderers.WriteCvar();

	// the image settings share this page's one Apply row in theme mode
	if( UI_ThemeActive( ))
		WriteVideoOptions();

	if( isWindowedModeChanged )
		EngFuncs::CvarSetValue( "fullscreen", currentWindowModeIndex );

	if( isVidModeChanged )
	{
		SetMode( currentModeIndex );
		EngFuncs::CvarSetValue( "vid_mode", currentModeIndex );
		vidList.SetCurrentIndex( currentModeIndex );
	}

	// let the test mode dialog have the screen to itself, it's on a timer
	if( testMode )
		return;

	if( IsRendererChanged( ))
		restartMsgBox.Show();
	else
		Hide();
}

void CMenuVidModes::FinalizeChanges()
{
	prevMode = EngFuncs::GetCvarFloat( "vid_mode" );
	prevFullscreen = EngFuncs::GetCvarFloat( "fullscreen" );
	prevModeX = EngFuncs::GetCvarFloat( "width" );
	prevModeY = EngFuncs::GetCvarFloat( "height" );
}

void CMenuVidModes::ApplyChanges()
{
	if( testModeMsgBox.IsVisible( ))
		return;

	FinalizeChanges();
}

void CMenuVidModes::RevertChanges()
{
	bool fullscreenChanged = false;

	// if we switched FROM fullscreen TO windowed, then we must get rid of fullscreen mode
	// so we can easily change the window size, without jerking display mode
	if( prevFullscreen == false && EngFuncs::GetCvarFloat( "fullscreen" ) != 0.0f )
	{
		EngFuncs::CvarSetValue( "fullscreen", prevFullscreen );
		fullscreenChanged = true;
	}

	SetMode( prevModeX, prevModeY );

	// otherwise, we better to set window size at first, then switch TO fullscreen
	if( !fullscreenChanged )
		EngFuncs::CvarSetValue( "fullscreen", prevFullscreen );
}

void CMenuVidModes::Draw()
{
	if( testModeMsgBox.IsVisible( ) && !FBitSet( testModeMsgBox.iFlags, QMF_CLOSING ))
	{
		if( testModeTimer - gpGlobals->time > 0 )
		{
			snprintf( testModeMsg, sizeof( testModeMsg ), L( "Keep this resolution? %i seconds remaining" ), (int)(testModeTimer - gpGlobals->time) );
		}
		else
		{
			testModeMsgBox.Hide();
			RevertChanges();
		}
	}
	CMenuFramework::Draw();
}

/*
=================
UI_VidModes_Init
=================
*/
void CMenuVidModes::_Init( void )
{
	static const char *windowModeStr[] =
	{
		L( "Windowed" ),
		L( "Fullscreen" ),
		L( "Borderless" ),
	};
	static CStringArrayModel windowModeModel( windowModeStr, V_ARRAYSIZE( windowModeStr ));

	m_bCoF = UI_ThemeActive() && UI_IsCryOfFear();

	banner.SetPicture( ART_BANNER );

	// one Video page in theme mode: modes on the left, image settings on the
	// right. Without the theme this is still only the Video modes dialog.
	if( UI_ThemeActive( ))
		SetPanel( "VIDEO", 940, 620 );

	vidList.SetRect( 360, 230, -20, 365 );
	vidList.SetupColumn( 0, L( "GameUI_Resolution" ), 1.0f );
	vidList.SetModel( &vidListModel );

	vsync.szName = L( "GameUI_VSync" );
	vsync.SetCoord( 360, 670 );
	vsync.LinkCvar( "gl_vsync" );

	testModeMsgBox.SetMessage( testModeMsg );
	testModeMsgBox.onPositive = VoidCb( &CMenuVidModes::FinalizeChanges );
	testModeMsgBox.onNegative = VoidCb( &CMenuVidModes::RevertChanges );
	testModeMsgBox.Link( this );

	restartMsgBox.SetMessage( L( "Restart the game to apply changes" ) );
	restartMsgBox.onPositive = VoidCb( &CMenuVidModes::Hide );
	restartMsgBox.Link( this );

	renderersModel.Update();
	renderers.szName = L( "GameUI_Renderer" );
	renderers.Setup( &renderersModel );
	renderers.SetRect( 80, 480, 250, 32 );
	renderers.SetCharSize( QM_SMALLFONT );
	renderers.onCvarGet = VoidCb( &CMenuVidModes::GetRendererConfig );
	renderers.onCvarWrite = VoidCb( &CMenuVidModes::WriteRendererConfig );

	windowMode.szName = L( "Window mode" );
	windowMode.Setup( &windowModeModel );
	windowMode.SetRect( 80, 550, 250, 32 );
	windowMode.SetCharSize( QM_SMALLFONT );
	SET_EVENT_MULTI( windowMode.onChanged,
	{
		// disable vid modes list when borderless is used
		CMenuVidModes *parent = pSelf->GetParent( CMenuVidModes );
		parent->vidList.SetGrayed( parent->windowMode.GetCurrentValue( ) == 2 );
		parent->vidList.SetInactive( parent->windowMode.GetCurrentValue( ) == 2 );
	});

	if( !UI_ThemeActive( ))
		AddItem( banner );

	// secondary first, so the button row reads Cancel then Apply
	if( UI_ThemeActive( ))
	{
		AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuVidModes::Hide ) );
		AddButton( L( "GameUI_Apply" ), nullptr, PC_OK, VoidCb( &CMenuVidModes::SetConfig ) );
	}
	else
	{
		AddButton( L( "GameUI_Apply" ), nullptr, PC_OK, VoidCb( &CMenuVidModes::SetConfig ) );
		AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuVidModes::Hide ) );
	}

	AddItem( renderers );
	AddItem( windowMode );
	AddItem( vsync );
	AddItem( vidList );

	if( UI_ThemeActive( ))
		InitVideoOptions();

	renderers.LinkCvar( "r_refdll", CMenuEditable::CVAR_STRING );
}

void CMenuVidModes::_VidInit()
{
	if( !IsPanel( ))
		return;

	Rect left = ColumnRect( 0, 2 );
	Rect right = ColumnRect( 1, 2 );
	int y = left.pt.y + THEME_LABEL_TALL + 8;

	renderers.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
	y += THEME_CTRL_PITCH;
	windowMode.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
	y += THEME_CTRL_PITCH;
	vsync.SetCoord( left.pt.x, y );
	y += THEME_ROW_PITCH + 8;

	// the mode list takes the rest of the left column
	vidList.SetRect( left.pt.x, y + ( UI_CoFSelectionLists() ? THEME_LABEL_TALL : 0 ), left.sz.w,
		UI_CoFSelectionLists() ? THEME_CTRL_H : left.pt.y + left.sz.h - y );

	LayoutVideoOptions( right );
}

void CMenuVidModes::Reload()
{
	GetConfig();

	if( UI_ThemeActive( ))
		ReloadVideoOptions();

	CMenuFramework::Reload();
}

ADD_MENU( menu_vidmodes, CMenuVidModes, UI_VidModes_Menu )
