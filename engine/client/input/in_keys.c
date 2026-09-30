/*
keys.c - console key events
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
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform/platform.h"

typedef struct
{
	const char *binding;
	uint       down     : 1;
	uint       gamedown : 1;
	uint       repeats  : 30; // if > 1, it is autorepeating
} enginekey_t;

typedef struct keyname_s
{
	const char *name;    // key name
	int        keynum;   // key number
	const char *binding; // default bind
} keyname_t;

// 512 is the maximum amount of scancodes supported by SDL2
// but we don't use that much, only about 255 like Quake/HL, add 9 additional
// keys for international keyboards, that gives us 264 keys total
//
// as no limit is imposed by client.dll API, this can be safely extended
// if needed
static enginekey_t keys[265];
static CVAR_DEFINE_AUTO( cof_panel_escape, "1", FCVAR_ARCHIVE, "Cry of Fear: Escape closes the open game pane before opening pause (0 restores engine-first Escape)" );
static qboolean cof_panel_escape_down[ARRAYSIZE(keys)];

// Capture the complete physical press, even if closing the pane changes key_dest.
static qboolean Key_CoF_PanelEscape( int key, qboolean down, const char *binding )
{
 if(cof_panel_escape_down[key])
 {
  if(!down) cof_panel_escape_down[key]=false;
  keys[key].gamedown=false; keys[key].repeats=0;
  VGui_CoF_ForgetKey(key);
  return true;
 }
 if(!down || !cof_panel_escape.value || cls.key_dest!=key_game || cls.state!=ca_active || cl.background ||
  !GI || Q_stricmp(GI->gamefolder,"cryoffear") || !VGui_IsActive()) return false;
 if(key!=K_ESCAPE && !(key==K_START_BUTTON && binding && !Q_stricmp(binding,"cancelselect"))) return false;
 qboolean reporting=false;
 unsigned int classes=CL_CoF_PanelsOnScreen(&reporting);
 if(!reporting || !classes) return false;
 cof_panel_escape_down[key]=true;
 keys[key].gamedown=false; keys[key].repeats=0;
 VGui_CoF_ForgetKey(key);
 char info[256];
 if(VGui_CoF_PanelAction(COF_VGUI_PANEL_BACK,info,sizeof(info))) return true;
 if(classes==COF_PANEL_INVENTORY)
 {
  clgame.dllFuncs.pfnKey_Event(true,key,"+inventory");
  clgame.dllFuncs.pfnKey_Event(false,key,"+inventory");
 }
 else
 {
  clgame.dllFuncs.pfnKey_Event(true,K_ESCAPE,"cancelselect");
  clgame.dllFuncs.pfnKey_Event(false,K_ESCAPE,"cancelselect");
 }
 return true; // never open pause on the same physical press that was offered to a pane
}


// where the menu or the client edits the text, in render coordinates
static struct
{
	int x, y, w, h;
} key_textinput_rect;

// real platform text input state, might differ from host.textmode with BUGCOMP_ALWAYS_ENABLE_TEXT_INPUT
static qboolean key_textinput_enabled;

static const keyname_t keynames[] =
{
{ "TAB",            K_TAB,                 ""                },
{ "ENTER",		    K_ENTER,               ""                },
{ "ESCAPE",         K_ESCAPE,              "cancelselect"    }, // hardcoded
{ "SPACE",          K_SPACE,               "+jump"           },
{ "BACKSPACE",      K_BACKSPACE,           ""                },
{ "UPARROW",        K_UPARROW,             "+forward"        },
{ "DOWNARROW",      K_DOWNARROW,           "+back"           },
{ "LEFTARROW",      K_LEFTARROW,           "+left"           },
{ "RIGHTARROW",     K_RIGHTARROW,          "+right"          },
{ "ALT",            K_ALT,                 "+strafe"         },
{ "CTRL",           K_CTRL,                "+attack"         },
{ "SHIFT",          K_SHIFT,               "+speed"          },
{ "CAPSLOCK",       K_CAPSLOCK,            ""                },
{ "SCROLLOCK",      K_SCROLLLOCK,          ""                },
{ "F1",             K_F1,                  "cmd help"        },
{ "F2",             K_F2,                  "menu_savegame"   },
{ "F3",             K_F3,                  "menu_loadgame"   },
{ "F4",             K_F4,                  "menu_controls"   },
{ "F5",             K_F5,                  "menu_creategame" },
{ "F6",             K_F6,                  "savequick"       },
{ "F7",             K_F7,                  "loadquick"       },
{ "F8",             K_F8,                  "stop"            },
{ "F9",             K_F9,                  ""                },
{ "F10",            K_F10,                 "menu_main"       },
{ "F11",            K_F11,                 ""                },
{ "F12",            K_F12,                 "snapshot"        },
{ "INS",            K_INS,                 ""                },
{ "DEL",            K_DEL,                 "+lookdown"       },
{ "PGDN",           K_PGDN,                "+lookup"         },
{ "PGUP",           K_PGUP,                ""                },
{ "HOME",           K_HOME,                ""                },
{ "END",            K_END,                 "centerview"      },

// mouse buttouns
{ "MOUSE1",         K_MOUSE1,              "+attack"         },
{ "MOUSE2",         K_MOUSE2,              "+attack2"        },
{ "MOUSE3",         K_MOUSE3,              ""                },
{ "MOUSE4",         K_MOUSE4,              ""                },
{ "MOUSE5",         K_MOUSE5,              ""                },
{ "MWHEELUP",       K_MWHEELUP,            ""                },
{ "MWHEELDOWN",     K_MWHEELDOWN,          ""                },

// digital keyboard
{ "KP_HOME",        K_KP_HOME,             ""                },
{ "KP_UPARROW",     K_KP_UPARROW,          "+forward"        },
{ "KP_PGUP",        K_KP_PGUP,             ""                },
{ "KP_LEFTARROW",   K_KP_LEFTARROW,        "+left"           },
{ "KP_5",           K_KP_5,                ""                },
{ "KP_RIGHTARROW",  K_KP_RIGHTARROW,       "+right"          },
{ "KP_END",         K_KP_END,              "centerview"      },
{ "KP_DOWNARROW",   K_KP_DOWNARROW,        "+back"           },
{ "KP_PGDN",        K_KP_PGDN,             "+lookup"         },
{ "KP_ENTER",       K_KP_ENTER,            ""                },
{ "KP_INS",         K_KP_INS,              ""                },
{ "KP_DEL",         K_KP_DEL,              "+lookdown"       },
{ "KP_SLASH",       K_KP_SLASH,            ""                },
{ "KP_MINUS",       K_KP_MINUS,            ""                },
{ "KP_PLUS",        K_KP_PLUS,             ""                },
{ "PAUSE",          K_PAUSE,               "pause"           },

// Gamepad
// A/B X/Y names match the Xbox controller layout
{ "A_BUTTON",       K_A_BUTTON,            "+jump"           },
{ "B_BUTTON",       K_B_BUTTON,            "+use"            },
{ "X_BUTTON",       K_X_BUTTON,            "+reload"         },
{ "Y_BUTTON",       K_Y_BUTTON,            "impulse 100"     }, // Flashlight
{ "BACK",           K_BACK_BUTTON,         "pause"           }, // Menu
{ "MODE",           K_MODE_BUTTON,         ""                },
{ "START",          K_START_BUTTON,        "cancelselect"    },
{ "STICK1",         K_LSTICK,              "+speed"          },
{ "STICK2",         K_RSTICK,              "+duck"           },
{ "L1_BUTTON",      K_L1_BUTTON,           "+duck"           },
{ "R1_BUTTON",      K_R1_BUTTON,           "+attack"         },
{ "DPAD_UP",        K_DPAD_UP,             "impulse 201"     }, // Spray
{ "DPAD_DOWN",      K_DPAD_DOWN,           "lastinv"         },
{ "DPAD_LEFT",      K_DPAD_LEFT,           "invprev"         },
{ "DPAD_RIGHT",     K_DPAD_RIGHT,          "invnext"         },
{ "L2_BUTTON",      K_L2_BUTTON,           "+speed"          },
{ "R2_BUTTON",      K_R2_BUTTON,           "+attack2"        },
{ "LTRIGGER",       K_JOY1,                "+speed"          }, // L2 in SDL2
{ "RTRIGGER",       K_JOY2,                "+attack2"        }, // R2 in SDL2
{ "JOY3",           K_JOY3,                ""                },
{ "JOY4",           K_JOY4,                ""                },
{ "C_BUTTON",       K_C_BUTTON,            ""                },
{ "Z_BUTTON",       K_Z_BUTTON,            ""                },
{ "MISC_BUTTON",    K_MISC_BUTTON,         ""                },
{ "PADDLE1",        K_PADDLE1_BUTTON,      ""                },
{ "PADDLE2",        K_PADDLE2_BUTTON,      ""                },
{ "PADDLE3",        K_PADDLE3_BUTTON,      ""                },
{ "PADDLE4",        K_PADDLE4_BUTTON,      ""                },
{ "TOUCHPAD",       K_TOUCHPAD,            ""                },
{ "MISC2_BUTTON",   K_MISC2_BUTTON,        ""                },
{ "AUX27",          K_AUX27,               ""                }, // generic
{ "AUX28",          K_AUX28,               ""                },
{ "AUX29",          K_AUX29,               ""                },
{ "AUX30",          K_AUX30,               ""                },
{ "AUX31",          K_AUX31,               ""                },
{ "AUX32",          K_AUX32,               ""                },

// raw semicolon seperates commands
{ "SEMICOLON",      ';',                   ""                },

// extended keys set
{ "INTERNATIONAL1", K_INTERNATIONAL,       ""                },
{ "INTERNATIONAL2", K_INTERNATIONAL + 1,   ""                },
{ "INTERNATIONAL3", K_INTERNATIONAL + 2,   ""                },
{ "INTERNATIONAL4", K_INTERNATIONAL + 3,   ""                },
{ "INTERNATIONAL5", K_INTERNATIONAL + 4,   ""                },
{ "INTERNATIONAL6", K_INTERNATIONAL + 5,   ""                },
{ "INTERNATIONAL7", K_INTERNATIONAL + 6,   ""                },
{ "INTERNATIONAL8", K_INTERNATIONAL + 7,   ""                },
{ "INTERNATIONAL9", K_INTERNATIONAL + 8,   ""                },
};

// cof_key_probe: see Key_QueueBindText below
static qboolean key_probe_immediate;

static CVAR_DEFINE_AUTO( key_rotate, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "rotate arrow keys (0-3)" );

/*
===================
Key_IsDown
===================
*/
int GAME_EXPORT Key_IsDown( int keynum )
{
	if( keynum == -1 )
		return false;
	return keys[keynum].down;
}

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keys[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.

0x11 will be interpreted as raw hex, which will allow new controlers

to be configured even if they don't have defined names.
===================
*/
static int Key_StringToKeynum( const char *str )
{
	if( !str || !str[0] )
		return -1;

	if( !str[1] )
		return str[0];

	// check for hex code
	if( str[0] == '0' && str[1] == 'x' )
	{
		int key = Q_atoi( str );

		if( key >= ARRAYSIZE( keys ))
			return -1;

		return key;
	}

	// scan for a text match
	for( int i = 0; i < ARRAYSIZE( keynames ); i++ )
	{
		if( !Q_stricmp( str, keynames[i].name ))
			return keynames[i].keynum;
	}

	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, a K_* name, or a 0x11 hex string) for the
given keynum.
===================
*/
const char *Key_KeynumToString( int keynum )
{
	static char	tinystr[16];

	if( keynum == -1 )
		return "<KEY NOT FOUND>";

	if( keynum < 0 || keynum >= ARRAYSIZE( keys ))
		return "<OUT OF RANGE>";

	// check for printable ascii (don't use quote)
	if( keynum > 32 && keynum < 127 && keynum != '"' && keynum != ';' && keynum != K_SCROLLLOCK )
	{
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	// check for a key string
	for( int i = 0; i < ARRAYSIZE( keynames ); i++ )
	{
		if( keynum == keynames[i].keynum )
			return keynames[i].name;
	}

	Q_snprintf( tinystr, sizeof( tinystr ), "0x%x", keynum );
	return tinystr;
}

/*
===================
Key_SetBinding
===================
*/
void GAME_EXPORT Key_SetBinding( int keynum, const char *binding )
{
	if( keynum == -1 ) return;

	// free old bindings
	if( keys[keynum].binding )
	{
		Mem_Free((char *)keys[keynum].binding );
		keys[keynum].binding = NULL;
	}

	// allocate memory for new binding
	keys[keynum].binding = copystring( binding );
}


/*
===================
Key_GetBinding
===================
*/
const char *Key_GetBinding( int keynum )
{
	if( keynum == -1 )
		return NULL;
	return keys[keynum].binding;
}

/*
===================
Key_GetKey
===================
*/
static int Key_GetKey( const char *pBinding )
{
	if( !pBinding ) return -1;

	int len = Q_strlen( pBinding );

	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( !keys[i].binding )
			continue;

		const char *p = keys[i].binding;

		if( *p == '+' )
			p++;

		if( !Q_strnicmp( p, pBinding, len ) )
			return i;
	}

	return -1;
}

/*
=============
Key_LookupBinding

=============
*/
const char *Key_LookupBinding( const char *pBinding )
{
	int key = Key_GetKey( pBinding );

	if( key == -1 )
		return NULL;

	return Key_KeynumToString( key );
}

/*
===================
Key_Unbind_f
===================
*/
static void Key_Unbind_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "unbind <key> : remove commands from a key\n" );
		return;
	}

	int b = Key_StringToKeynum( Cmd_Argv( 1 ));

	if( b == -1 )
	{
		Con_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ));
		return;
	}

	if( b == K_ESCAPE )
	{
		Con_Printf( "Can't unbind ESCAPE key\n" );
		return;
	}

	Key_SetBinding( b, "" );
}

/*
===================
Key_Unbindall_f
===================
*/
static void Key_Unbindall_f( void )
{
	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( keys[i].binding )
			Key_SetBinding( i, "" );
	}

	// set some defaults
	Key_SetBinding( K_ESCAPE, "cancelselect" );
	Key_SetBinding( K_START_BUTTON, "cancelselect" );
}

/*
===================
Key_Reset_f
===================
*/
static void Key_Reset_f( void )
{
	// clear all keys first
	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( keys[i].binding )
			Key_SetBinding( i, "" );
	}

	// apply default values
	for( int i = 0; i < ARRAYSIZE( keynames ); i++ )
		Key_SetBinding( keynames[i].keynum, keynames[i].binding );
}

/*
===================
Key_Bind_f
===================
*/
static void Key_Bind_f( void )
{
	int c = Cmd_Argc();

	if( c < 2 )
	{
		Con_Printf( S_USAGE "bind <key> [command] : attach a command to a key\n" );
		return;
	}

	int b = Key_StringToKeynum( Cmd_Argv( 1 ));

	if( b == -1 )
	{
		Con_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ));
		return;
	}

	if( c == 2 )
	{
		if( keys[b].binding )
			Con_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), keys[b].binding );
		else Con_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ));
		return;
	}

	// copy the rest of the command line
	char cmd[1024];
	cmd[0] = 0; // start out with a null string

	for( int i = 2; i < c; i++ )
	{
		Q_strncat( cmd, Cmd_Argv( i ), sizeof( cmd ));
		if( i != ( c - 1 )) Q_strncat( cmd, " ", sizeof( cmd ));
	}

	Key_SetBinding( b, cmd );
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings( file_t *f )
{
	if( !f ) return;

	FS_Printf( f, "unbindall\n" );

	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( COM_StringEmptyOrNULL( keys[i].binding ))
			continue;

		string newCommand;
		Cmd_Escape( newCommand, keys[i].binding, sizeof( newCommand ));

		// NOTE: as TheKingFireS figured out, some particular mods (like CoF) do not
		// use LookupBinding to find key by it's binding and instead parse config.cfg
		// to look for bind commands.
		// CoF expects key to be enclosed in double quotes
		FS_Printf( f, "bind \"%s\" \"%s\"\n", Key_KeynumToString( i ), newCommand );
	}
}

/*
============
Key_Bindlist_f

============
*/
static void Key_Bindlist_f( void )
{
	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( COM_StringEmptyOrNULL( keys[i].binding ))
			continue;

		Con_Printf( "%s \"%s\"\n", Key_KeynumToString( i ), keys[i].binding );
	}
}

/*
=====================================
Cmd_GetKeysList

Autocomplete for bind command
=====================================
*/
qboolean Cmd_GetKeysList( const char *s, char *completedname, int length, qboolean print_suggestions )
{
	size_t i, numkeys;
	string keys_strings[ARRAYSIZE( keys )];
	string matchbuf;

	// compare keys list with current keyword
	int len = Q_strlen( s );

	for( i = 0, numkeys = 0; i < ARRAYSIZE( keys ); i++ )
	{
		const char *keyname = Key_KeynumToString( i );

		if(( *s == '*' ) || !Q_strnicmp( keyname, s, len ))
			Q_strncpy( keys_strings[numkeys++], keyname, sizeof( keys_strings[0] ));
	}

	if( !numkeys ) return false;
	Q_strncpy( matchbuf, keys_strings[0], sizeof( matchbuf ));
	if( completedname && length )
		Q_strncpy( completedname, matchbuf, length );
	if( numkeys == 1 ) return true;

	for( i = 0; i < numkeys; i++ )
	{
		Q_strncpy( matchbuf, keys_strings[i], sizeof( matchbuf ));
		if( print_suggestions )
			Con_Printf( "%16s\n", matchbuf );
	}

	if( print_suggestions )
		Con_Printf( "\n^3 %zu keys found.\n", numkeys );

	if( completedname && length )
	{
		for( i = 0; matchbuf[i]; i++ )
		{
			if( Q_tolower( completedname[i] ) != Q_tolower( matchbuf[i] ))
				completedname[i] = 0;
		}
	}

	return true;
}

/*
==============================================================================

			LINE TYPING INTO THE CONSOLE

==============================================================================
*/
/*
===================
Key_CoFProbe_f

cof_key_probe <key> <1|0> - deliver one key press or release straight into
Key_Event, the same entry point the platform layer uses.

This project may not inject OS keyboard or mouse input (a worker's injected
keystrokes once landed in the user's editor), and Key_Event is internal, so
there was no way to exercise a *bound key* from a cfg at all: running the bound
command by hand goes through Cbuf and skips everything Key_Event does (the
client's key hook, the bind expansion with its key number, the release of a
button bound to a "+command"). This command closes that gap and nothing else:
it does not synthesise mouse motion, it does not touch the platform event
queue, it is restricted, so a server cannot reach it, and it only runs with
"developer" set. Used to verify the aim-down-sights modes (cof_ads_toggle).
===================
*/
static void Key_CoFProbe_f( void )
{
	int	key, down;

	if( !host_developer.value )
	{
		Con_Printf( "cof_key_probe is a developer command: set \"developer 1\" first\n" );
		return;
	}

	if( Cmd_Argc( ) != 3 )
	{
		Con_Printf( S_USAGE "cof_key_probe <key> <1|0> : deliver a key event to the engine's own key path\n" );
		return;
	}

	key = Key_StringToKeynum( Cmd_Argv( 1 ));

	if( key == -1 || key >= ARRAYSIZE( keys ))
	{
		Con_Printf( "cof_key_probe: \"%s\" isn't a valid key\n", Cmd_Argv( 1 ));
		return;
	}

	down = Q_atoi( Cmd_Argv( 2 )) != 0;

	Con_Printf( "[cof-key-probe] %s %s (bind \"%s\", key_dest %i)\n",
		Key_KeynumToString( key ), down ? "down" : "up",
		keys[key].binding ? keys[key].binding : "", (int)cls.key_dest );

	key_probe_immediate = true;
	Key_Event( key, down );
	key_probe_immediate = false;
}

/*
===================
Key_Init
===================
*/
void Key_Init( void )
{
	// register our functions
	Cmd_AddRestrictedCommand( "bind", Key_Bind_f, "binds a command to the specified key in bindmap" );
	Cmd_AddRestrictedCommand( "unbind", Key_Unbind_f, "removes a command on the specified key in bindmap" );
	Cmd_AddRestrictedCommand( "unbindall", Key_Unbindall_f, "removes all commands from all keys in bindmap" );
	Cmd_AddRestrictedCommand( "resetkeys", Key_Reset_f, "reset all keys to their default values" );
	Cmd_AddCommand( "bindlist", Key_Bindlist_f, "display current key bindings" );
	Cmd_AddRestrictedCommand( "cof_key_probe", Key_CoFProbe_f, "developer: deliver a key press (1) or release (0) to the engine key path, without any OS input" );
	Cmd_AddCommand( "makehelp", Key_EnumCmds_f, "write help.txt that contains all console cvars and cmds" );

	// setup default binding. "unbindall" from config.cfg will be reset it
	for( int i = 0; i < ARRAYSIZE( keynames ); i++ )
		Key_SetBinding( keynames[i].keynum, keynames[i].binding );

	Cvar_RegisterVariable( &key_rotate );
	Cvar_RegisterVariable( &cof_panel_escape );

	// Cry of Fear on-screen keyboard, the engine half (cof_osk.c)
	CL_CoF_OskInit();

	// Cry of Fear Enter hook (cof_enter_hook.c)
	CL_CoF_EnterInit();
}

/*
===================
Key_QueueBindText

Where a bind's expanded "+cmd"/"-cmd" text goes.

Normally the end of the command buffer, exactly as upstream does it. The one
exception is cof_key_probe, which delivers a key event from INSIDE the command
buffer: a plain Cbuf_AddText would then queue the bind behind the whole rest of
the test script - typically a few hundred "wait"s - and the command would not
run until the script had finished, which is neither what a real key press does
nor anything a test can observe. While the probe is running the text is
inserted at the head of the buffer instead, which is where a key pressed on
this frame lands.

MEASURED, 2026-09-22 (stage1/ads-toggle-20260922/evidence): with Cbuf_AddText
the probe's "+attack3 243" never reached the client before the test script's
own "quit" (ads5.log, every cof_ads_status still at fov 90); with the insert
the client zooms on the frame the press was delivered (ads6.log,
stage1/m5b-integration-20260922/evidence/adsn*.log).
===================
*/
void Key_QueueBindText( const char *text )
{
	if( key_probe_immediate )
		Cbuf_InsertText( text );
	else Cbuf_AddText( text );
}

/*
===================
Key_AddKeyCommands
===================
*/
static void Key_AddKeyCommands( int key, const char *kb, qboolean down )
{
	if( !kb ) return;

	char button[1024];
	char *buttonPtr = button;

	for( int i = 0; ; i++ )
	{
		if( kb[i] == ';' || !kb[i] )
		{
			*buttonPtr = '\0';
			if( button[0] == '+' && !Q_stricmp( button, "+attack3" ) && CL_CoF_ADSHoldActive( ))
			{
				// Cry of Fear "Aim down sights: Hold": the aim key's press
				// and release each become one toggle pulse (cl_main.c)
				CL_CoF_ADSHoldKey( key, down );
			}
			else if( button[0] == '+' )
			{
				char cmd[1024];

				// button commands add keynum as a parm
				if( down ) Q_snprintf( cmd, sizeof( cmd ), "%s %i\n", button, key );
				else Q_snprintf( cmd, sizeof( cmd ), "-%s %i\n", button + 1, key );
				Key_QueueBindText( cmd );
			}
			else if( down )
			{
				// down-only command
				Key_QueueBindText( button );
				Key_QueueBindText( "\n" );
			}

			buttonPtr = button;
			while((((byte)kb[i]) <= ' ' || kb[i] == ';' ) && kb[i] != 0 )
				i++;
		}

		*buttonPtr++ = kb[i];
		if( !kb[i] ) break;
	}
}

/*
===================
Key_IsAllowedAutoRepeat

List of keys that allows auto-repeat
===================
*/
static qboolean Key_IsAllowedAutoRepeat( int key )
{
	if( cls.key_dest != key_game )
		return true;

	switch( key )
	{
	case K_BACKSPACE:
	case K_PAUSE:
	case K_PGUP:
	case K_KP_PGUP:
	case K_PGDN:
	case K_KP_PGDN:
		return true;
	default:
		return false;
	}
}

static int Key_Rotate( int key )
{
	if( key_rotate.value == 1.0f ) // CW
	{
		if( key == K_UPARROW )
				key = K_LEFTARROW;
		else if( key == K_LEFTARROW )
				key = K_DOWNARROW;
		else if( key == K_RIGHTARROW )
				key = K_UPARROW;
		else if( key == K_DOWNARROW )
				key = K_RIGHTARROW;
	}

	else if( key_rotate.value == 3.0f ) // CCW
	{
		if( key == K_UPARROW )
				key = K_RIGHTARROW;
		else if( key == K_LEFTARROW )
				key = K_UPARROW;
		else if( key == K_RIGHTARROW )
				key = K_DOWNARROW;
		else if( key == K_DOWNARROW )
				key = K_LEFTARROW;
	}

	else if( key_rotate.value == 2.0f )
	{
		if( key == K_UPARROW )
				key = K_DOWNARROW;
		else if( key == K_LEFTARROW )
				key = K_RIGHTARROW;
		else if( key == K_RIGHTARROW )
				key = K_LEFTARROW;
		else if( key == K_DOWNARROW )
				key = K_UPARROW;
	}

	return key;
}


/*
===================
Key_Event

Called by the system for both key up and key down events
===================
*/
void GAME_EXPORT Key_Event( int key, int down )
{
	key = Key_Rotate( key );

    // Finish a pane-owned press before OSK/Enter can acquire its repeat/release.
    // Fresh presses still use the normal OSK/Enter priority below.
    if( key>=0 && key<ARRAYSIZE(keys) && cof_panel_escape_down[key] )
    {
        keys[key].down=down?true:false;
        Key_CoF_PanelEscape(key,down,keys[key].binding);
        return;
    }

	if( OSK_KeyEvent( key, down ) )
		return;

	// Cry of Fear on-screen keyboard: was the last key a gamepad's (cof_osk.c)
	CL_CoF_OskNoteKey( key, down );

	// Cry of Fear Enter hook: START while the game polls Windows for Enter
	// (the computer, a Press Enter card) is that Enter (cof_enter_hook.c)
	if( CL_CoF_EnterKeyEvent( key, down ))
		return;

	// key was pressed before engine was run
	if( !keys[key].down && !down )
		return;

	const char *kb = keys[key].binding;
	keys[key].down = down ? true : false;

	if( Key_CoF_PanelEscape( key, down, kb ))
		return;

#ifdef HACKS_RELATED_HLMODS
	if(( cls.key_dest == key_game ) && ( cls.state == ca_cinematic ) && ( key != K_ESCAPE || !down ))
	{
		// only escape passed when cinematic is playing
		// HLFX 0.6 bug: crash in vgui3.dll while press +attack during movie playback
		return;
	}
#endif
	// Cry of Fear unified UI: after optional pane-first handling above, Escape
	// reaches the engine so the engine menu can be opened, even while a client
	// VGUI panel is up. client.dll's TeamFortressViewport::KeyInput answers
	// "handled" for K_ESCAPE whenever one of its panels is visible (the 3D menu,
	// the keypad, the computer, the save selector, the phone), and the client
	// first-refusal block below would then return before CL_Escape_f is reached,
	// leaving the engine menu unreachable from those screens.
	//
	// This branch is deliberately narrow: only K_ESCAPE, only key down, only
	// while the game itself has focus, and only with the gate enabled. Every
	// other key keeps the upstream client-first behaviour, including the CoF
	// computer-input workaround for FWGS issue #1923 below. Neither the client
	// nor the VGUI support library is offered this key, so the client's own
	// escape handling cannot fire a second time alongside the engine menu.
	//
	// Cry of Fear gamepad (cof_gamepad.c): START gets the same bypass while a
	// clickable CoF panel is open, since the panel swallows it otherwise.
	if( cof_ui_input_gate.value && down && cls.key_dest == key_game && ( key == K_ESCAPE || CL_CoF_PadStartAsEscape( key, kb )))
	{
		if( r_showtextures.value )
		{
			// close texture atlas, same as the generic escape handler below
			Cvar_DirectSet( &r_showtextures, "0" );
			return;
		}

		keys[key].repeats++;

		// ignore autorepeat, otherwise holding escape flips the menu open and shut
		if( keys[key].repeats > 1 )
			return;

		keys[key].gamedown = false;

		// neither the client nor the support library saw this press, so make
		// sure the matching release is not delivered to a CoF panel either
		VGui_CoF_ForgetKey( key );

		Con_Reportf( "[cof-ui] escape routed to the engine, client key handling bypassed\n" );

		CL_Escape_f();
		return;
	}

	// Cry of Fear gamepad (cof_gamepad.c): on an open CoF panel A / X click
	// at the gamepad cursor and the close key closes the inventory; such a
	// press, and its release, never reach the client or the key's binding
	// (the release is matched in any key_dest, the pause menu may be up by then)
	if(( cls.key_dest == key_game || !down ) && CL_CoF_PadKeyEvent( key, down ))
		return;

	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_game && ( down || keys[key].gamedown ))
	{
		if( !clgame.dllFuncs.pfnKey_Event( down, key, keys[key].binding ))
		{
			if( keys[key].repeats == 0 && down )
			{
				keys[key].gamedown = true;
			}

			if( !down )
			{
				keys[key].gamedown = false;
				keys[key].repeats = 0;
			}

			// deliver keys regardless of whether the client.dll handled them or not
			// reproduces GoldSrc behavior, where keys are passed even if the client.dll claims to have handled them
			// this is needed for Cry of fear's Computer interface input, for more context, see https://github.com/FWGS/xash3d-fwgs/issues/1923
			VGui_KeyEvent( key, down );
			return;
		}
	}

	// update auto-repeat status
	if( down )
	{
		keys[key].repeats++;

		if( !Key_IsAllowedAutoRepeat( key ) && keys[key].repeats > 1 )
		{
			// ignore most autorepeats
			return;
		}

		if( key >= 200 && !kb )
			Con_Printf( "%s is unbound.\n", Key_KeynumToString( key ));
	}
	else
	{
		keys[key].gamedown = false;
		keys[key].repeats = 0;
	}

	VGui_KeyEvent( key, down );

	// console key is hardcoded, so the user can never unbind it
	if( key == '`' || key == '~' )
	{
		// we are in typing mode, so don't switch to console
		if( cls.key_dest == key_message || !down )
			return;

		Con_ToggleConsole_f();
		return;
	}

	// escape is always handled special
	if( key == K_ESCAPE && down )
	{
		switch( cls.key_dest )
		{
		case key_game:
			if( r_showtextures.value )
			{
				// close texture atlas
				Cvar_DirectSet( &r_showtextures, "0" );
				return;
			}
			// Note: we don't offer the client key handling here since reaching this line proves the client already declined handling it.
			// this prevents accidentally declining a key the client asked the engine to handle. For more context, see https://github.com/FWGS/xash3d-fwgs/issues/1943
			
			// call escape immediately within the engine, since the client may not handle it (Natural Selection doesn't. See https://github.com/FWGS/xash3d-fwgs/issues/528)
			CL_Escape_f();
			return;
		default:
			break;
		}
	}

	if( cls.key_dest == key_menu )
	{
		// classic Xash3D menus don't have an extension that tells engine
		// to enable text input
		if( !gameui.use_extended_api )
		{
			// we don't know if menu wants text input or not
			// enable it unconditionally
			Key_EnableTextInput( true, false );

			// pass this key to the menu, if printable
			if( !host.textmode && down && ( key >= 32 ) && ( key <= 'z' ))
			{
				if( Key_IsDown( K_SHIFT ))
					key += 'A' - 'a';

				UI_CharEvent( key );
			}
		}

		UI_KeyEvent( key, down );
		return;
	}

	// key up events only perform actions if the game key binding is
	// a button command (leading + sign).  These will be processed even in
	// console mode and menu mode, to keep the character from continuing
	// an action started before a mode switch.
	if( !down )
	{
		Key_AddKeyCommands( key, kb, down );
		return;
	}

	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_game )
	{
		Key_AddKeyCommands( key, kb, down );
	}
	else if( cls.key_dest == key_console )
	{
		Key_Console( key );
	}
	else if( cls.key_dest == key_message )
	{
		Key_Message( key );
	}
}

/*
================
Key_GetTextInputRect

================
*/
static void Key_GetTextInputRect( int *x, int *y, int *w, int *h )
{
	if( Con_GetInputRect( x, y, w, h ))
		return;

	if( key_textinput_rect.w > 0 && key_textinput_rect.h > 0 )
	{
		*x = key_textinput_rect.x;
		*y = key_textinput_rect.y;
		*w = key_textinput_rect.w;
		*h = key_textinput_rect.h;
		return;
	}

	// nobody told us where the text is edited, keep the whole screen visible
	*x = *y = 0;
	*w = refState.width;
	*h = refState.height;
}

/*
================
Key_SetTextInputRect

================
*/
void Key_SetTextInputRect( int x, int y, int w, int h )
{
	if( key_textinput_rect.x == x && key_textinput_rect.y == y && key_textinput_rect.w == w && key_textinput_rect.h == h )
		return;

	key_textinput_rect.x = x;
	key_textinput_rect.y = y;
	key_textinput_rect.w = w;
	key_textinput_rect.h = h;

	// text is already edited somewhere else now, let the platform know immediately
	if( key_textinput_enabled && !osk_enable.value )
	{
		Key_GetTextInputRect( &x, &y, &w, &h );
		Platform_EnableTextInput( true, x, y, w, h );
	}
}

/*
================
Key_EnableTextInput

================
*/
void Key_EnableTextInput( qboolean enable, qboolean force )
{
	if( osk_enable.value )
	{
		OSK_EnableTextInput( enable, force );
		return;
	}

	// some mods (like Natural Selection) pull text events out of the platform event queue on their own and expect them to always arrive during the gameplay, like they do in GoldSrc
	qboolean enable_platform = enable || ( cls.key_dest == key_game && FBitSet( host.bugcomp, BUGCOMP_ALWAYS_ENABLE_TEXT_INPUT ));

	if( !enable && ( host.textmode || force ))
	{
		// don't reuse this rect for whoever enables the text input next
		memset( &key_textinput_rect, 0, sizeof( key_textinput_rect ));
	}

	// host.textmode is compared against here to pick up the new rect when the engine
	// leaves the text mode but the platform text input is kept running
	if( enable_platform && ( !key_textinput_enabled || host.textmode != enable || force ))
	{
		int x, y, w, h;

		Key_GetTextInputRect( &x, &y, &w, &h );
		Platform_EnableTextInput( true, x, y, w, h );
	}
	else if( !enable_platform && ( key_textinput_enabled || force ))
	{
		Platform_EnableTextInput( false, 0, 0, 0, 0 );
	}

	key_textinput_enabled = enable_platform;
	host.textmode = enable;
}

/*
=========
Key_SetKeyDest
=========
*/
void GAME_EXPORT Key_SetKeyDest( int key_dest )
{
	IN_ToggleClientMouse( key_dest, cls.key_dest );

	switch( key_dest )
	{
	case key_game:
		cls.key_dest = key_game;
		Key_EnableTextInput( false, false );
		break;
	case key_menu:
		cls.key_dest = key_menu;
		Key_EnableTextInput( false, false );
		break;
	case key_console:
		cls.key_dest = key_console;
#if !XASH_NSWITCH && !XASH_PSVITA // if we don't disable this, pops up the keyboard during load
		Key_EnableTextInput( true, false );
#endif
		break;
	case key_message:
		cls.key_dest = key_message;
		Key_EnableTextInput( true, false );
		break;
	default:
		Host_Error( "%s: wrong destination (%i)\n", __func__, key_dest );
		break;
	}
}

/*
===================
Key_ClearStates
===================
*/
void GAME_EXPORT Key_ClearStates( void )
{
	// don't clear keys during changelevel
	if( cls.changelevel )
		return;

	for( int i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( i < K_MOUSE1 || i > K_MOUSE5 )
			Key_Event( i, false ); // checks internally whether a key has been pressed or not

		keys[i].down = 0;
		keys[i].repeats = 0;
		keys[i].gamedown = 0;
	}

	memset( cof_panel_escape_down, 0, sizeof( cof_panel_escape_down ));

	// ensure that only actual mouse buttons are released
	IN_ClearMouseState();

	if( clgame.hInstance )
		clgame.dllFuncs.IN_ClearStates();
}

/*
===================
CL_CharEvent

Normal keyboard characters, already shifted / capslocked / etc
===================
*/
void CL_CharEvent( int key )
{
	if( cls.key_dest == key_console && !Con_Visible( ))
	{
		if((char)key == '`' || (char)key == '?' )
			return; // don't pass '`' when we open the console
	}

	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_console || cls.key_dest == key_message )
	{
		Con_CharEvent( key );
	}
	else if( cls.key_dest == key_menu )
	{
		UI_CharEvent( key );
	}
}

/*
============
Key_ToUpper

A helper function if platform input doesn't support text mode properly
============
*/
int Key_ToUpper( int keynum )
{
	if( keynum == '-' )
		return '_';
	if( keynum == '=' )
		return '+';
	if( keynum == ';' )
		return ':';
	if( keynum == '\'' )
		return '"';

	return Q_toupper( keynum );
}
