/*
KbActListModel.h -- shared model for gfx/shell/kb_act.lst
Copyright (C) 2026 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifndef KBACTLISTMODEL_H
#define KBACTLISTMODEL_H

#include "BaseModel.h"
#include "Utils.h"
#include "Theme.h"
#include "enginecallback_menu.h"
#include "utlvector.h"
#include "CoFOptions.h"
#include <ctype.h>

// Shared model over gfx/shell/kb_act.lst, the list of game actions used
// for the keyboard configuration screen and (read-only) the touch button
// command picker. Two view modes select the column projection:
//   VIEW_BINDINGS: action | key1 | key2   (Controls screen)
//   VIEW_PICKER:   action | command       (TouchButtons command picker)
// Consumers subclass to provide OnActivateEntry/OnDeleteEntry controllers.
class CMenuKbActListModel : public CMenuBaseModel
{
public:
	enum EView
	{
		VIEW_BINDINGS,
		VIEW_PICKER,
	};

	explicit CMenuKbActListModel( EView view ) : m_view( view ) { }

	struct entry_t
	{
		char display[96];  // colorized label
		char bind[64];     // empty when entry is a separator
		char first[20];    // formatted first bound key  (VIEW_BINDINGS only)
		char second[20];   // formatted second bound key (VIEW_BINDINGS only)
		bool header;       // a section caption (theme mode, see CollapseSections)
	};

	void Update() override
	{
		entries.RemoveAll();

		if( m_view == VIEW_PICKER )
			AddVirtualCommands();

		char *afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/kb_act.lst", NULL );
		if( !afile )
			return;

		char *pfile = afile;
		char token[64];

		while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			entry_t e = { 0 };

			if( !stricmp( token, "blank" ))
			{
				// separator: next token is the label
				pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
				if( !pfile )
					break;

				FormatLabel( token, e.display, sizeof( e.display ));
			}
			else
			{
				Q_strncpy( e.bind, token, sizeof( e.bind ));

				pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
				if( !pfile )
					break;

				// Cry of Fear options layout: the game's "Tertiary Attack"
				// (+attack3) is its aim down sights, so the list calls it "Aim"
				if( m_view == VIEW_BINDINGS && UI_CoFOptionsLayout( ) && !stricmp( e.bind, "+attack3" ))
					FormatLabel( L( "Aim" ), e.display, sizeof( e.display ));
				else FormatLabel( token, e.display, sizeof( e.display ));

				if( m_view == VIEW_BINDINGS )
				{
					int keys[2];
					LookupBoundKeys( e.bind, keys );
					FormatKey( keys[0], e.first, sizeof( e.first ));
					FormatKey( keys[1], e.second, sizeof( e.second ));
				}
			}

			entries.AddToTail( e );
		}

		EngFuncs::COM_FreeFile( afile );

		// the sprint toggle and the quick turn (gamepad rounds 2 and 3),
		// added in code like the crouch toggle below (which lands between
		// Crouch and Quick turn)
		if( m_view == VIEW_BINDINGS && UI_CoFOptionsLayout( ))
		{
			AddAfter( "+sprint", "cof_sprint_toggle_press", L( "Sprint (toggle)" ));
			AddAfter( "+duck", "cof_quickturn", L( "Quick turn" ));
		}

		// Cry of Fear options layout: the engine's crouch toggle
		// (cof_duck_toggle, gamepad round) under the game's own Crouch row,
		// unless the list already has it - the game's kb_act.lst is not
		// shipped modified
		if( m_view == VIEW_BINDINGS && UI_CoFOptionsLayout( ))
			AddAfter( "+duck", "cof_duck_toggle", L( "Crouch (toggle)" ));

		// quick save / quick load (engine cof_quicksave / cof_quickload,
		// F5 / F9 by default, patches/cof-mainui-quicksave.patch) under the
		// game's own "Pause game" row, added in code like the crouch toggle
		if( m_view == VIEW_BINDINGS && UI_CoFOptionsLayout( ))
		{
			AddAfter( "pause", "cof_quicksave", L( "Quick save" ));
			AddAfter( "cof_quicksave", "cof_quickload", L( "Quick load" ));
		}

		if( UI_ThemeActive( ))
			CollapseSections();
	}

	void AddAfter( const char *after, const char *bind, const char *label )
	{
		int at = -1;

		for( int i = 0; i < entries.Count(); i++ )
		{
			if( !stricmp( entries[i].bind, bind ))
				return;
			if( !stricmp( entries[i].bind, after ))
				at = i;
		}

		entry_t e = { 0 };
		Q_strncpy( e.bind, bind, sizeof( e.bind ));
		FormatLabel( label, e.display, sizeof( e.display ));

		int keys[2];
		LookupBoundKeys( e.bind, keys );
		FormatKey( keys[0], e.first, sizeof( e.first ));
		FormatKey( keys[1], e.second, sizeof( e.second ));

		if( at < 0 )
			entries.AddToTail( e );
		else
			entries.InsertAfter( at, e );
	}

	/*
	=================
	CollapseSections

	gfx/shell/kb_act.lst marks a section the GoldSrc way:

	    "blank" "=========================="
	    "blank" "Controls - Movement"
	    "blank" "=========================="

	The WON list simply drew all three, which in a themed table reads as two
	rows of noise around the caption. Drop every rule-of-equals row and turn
	what is left of each separator into a real section caption, uppercased and
	free of colour codes; CMenuTable draws those as a small dim label with a
	hairline under it and skips them when navigating.
	=================
	*/
	void CollapseSections()
	{
		for( int i = entries.Count() - 1; i >= 0; i-- )
		{
			if( entries[i].bind[0] )
				continue; // a real binding row

			if( IsRule( entries[i].display ))
			{
				entries.Remove( i );
				continue;
			}

			entries[i].header = true;
			StripAndUpper( entries[i].display, sizeof( entries[i].display ));
		}
	}

	// true for a label that is nothing but '=' (and colour codes)
	static bool IsRule( const char *s )
	{
		bool any = false;

		for( ; *s; s++ )
		{
			if( IsColorString( s )) { s++; continue; }
			if( *s == '=' ) { any = true; continue; }
			return false;
		}
		return any;
	}

	static void StripAndUpper( char *s, size_t size )
	{
		char tmp[96];
		size_t n = 0;

		for( const char *p = s; *p && n < sizeof( tmp ) - 1; p++ )
		{
			if( IsColorString( p )) { p++; continue; }
			tmp[n++] = (char)toupper( (unsigned char)*p );
		}
		tmp[n] = 0;
		Q_strncpy( s, tmp, size );
	}

	int GetColumns() const override { return m_view == VIEW_BINDINGS ? 3 : 2; }
	int GetRows() const override { return entries.Count(); }

	const char *GetCellText( int line, int column ) override
	{
		if( !entries.IsValidIndex( line ))
			return NULL;

		const entry_t &e = entries[line];
		switch( column )
		{
		case 0: return e.display;
		case 1: return m_view == VIEW_BINDINGS ? e.first : e.bind;
		case 2: return m_view == VIEW_BINDINGS ? e.second : NULL;
		}
		return NULL;
	}

	bool IsCellTextWrapped( int line, int column ) override
	{
		return IsLineUsable( line );
	}

	bool IsLineUsable( int line ) const
	{
		return entries.IsValidIndex( line ) && entries[line].bind[0] != 0;
	}

	bool IsLineSectionHeader( int line ) const override
	{
		return entries.IsValidIndex( line ) && entries[line].header;
	}

	// Walk all keys, return up to two bound to `command` (in input order, then swapped).
	static void LookupBoundKeys( const char *command, int twoKeys[2] )
	{
		twoKeys[0] = twoKeys[1] = -1;

		for( int i = 0, count = 0; ; i++ )
		{
			const char *str = EngFuncs::KeynumToString( i );
			if( !strcmp( str, "<OUT OF RANGE>" ))
				break;

			const char *b = EngFuncs::KEY_GetBinding( i );
			if( !b )
				continue;

			// Cry of Fear options layout: pad buttons are the Gamepad
			// controls page's (menus/CoFOptions.cpp)
			if( UI_CoFOptionsLayout( ) && UI_CoFIsPadKey( i ))
				continue;

			if( !stricmp( command, b ))
			{
				twoKeys[count++] = i;
				if( count == 2 )
					break;
			}
		}

		if( twoKeys[0] != -1 && twoKeys[1] != -1 )
		{
			int tmp = twoKeys[1];
			twoKeys[1] = twoKeys[0];
			twoKeys[0] = tmp;
		}
	}

	CUtlVector<entry_t> entries;

protected:
	EView m_view;

private:
	void AddVirtualCommands()
	{
		// Engine-side virtual commands handled directly by Touch_SetCommand:
		// these reshape the button into a look pad, joystick, d-pad or wheel
		// rather than firing a regular console command.
		AddLabel( "==========================" );
		AddLabel( L( "Built-in touch actions" ));
		AddLabel( "==========================" );
		AddVirtualCommand( "_look", L( "Look (touchpad)" ));
		AddVirtualCommand( "_move", L( "Move (joystick)" ));
		AddVirtualCommand( "_joy", L( "Move (centered joystick)" ));
		AddVirtualCommand( "_dpad", L( "Move (D-Pad)" ));
		AddVirtualCommand( "_wheel <up> <down> <release> <press>", L( "Vertical wheel" ));
		AddVirtualCommand( "_hwheel <left> <right> <release> <press>", L( "Horizontal wheel" ));
	}

	void AddLabel( const char *label )
	{
		entry_t e = { 0 };
		FormatLabel( label, e.display, sizeof( e.display ));
		entries.AddToTail( e );
	}

	void AddVirtualCommand( const char *bind, const char *label )
	{
		entry_t e = { 0 };
		Q_strncpy( e.bind, bind, sizeof( e.bind ));
		FormatLabel( label, e.display, sizeof( e.display ));
		entries.AddToTail( e );
	}

	static void FormatLabel( const char *token, char *out, size_t size )
	{
		const char *text = token[0] == '#' ? L( token ) : token;
		snprintf( out, size, "^6%s^7", text );
	}

	static void FormatKey( int key, char *out, size_t size )
	{
		out[0] = 0;
		if( key == -1 )
			return;

		const char *s = EngFuncs::KeynumToString( key );
		if( !s )
			return;

		if( !strnicmp( s, "MOUSE", 5 ))
			snprintf( out, size, "^5%s^7", s );
		else
			snprintf( out, size, "^3%s^7", s );
	}
};

#endif // KBACTLISTMODEL_H
