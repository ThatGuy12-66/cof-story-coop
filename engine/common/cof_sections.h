/*
cof_sections.h - Cry of Fear story co-op: merged story maps, the engine's side
GPL-3.0-or-later.

Setup merges door-joined story maps into one map file (tools/map_merge, the launcher repo): member maps stacked in
height, geometry only, plus maps/<name>.sections, the rules for each member ("section"). The engine reads the
rules (CoF_SectionsParse) and puts the merged map's entities together at load from each member's own entity
string (CoF_ComposeEntities): origins moved up by the member's height, brush models renumbered, names renamed so
nothing clashes or wires across sections, switchable light styles mapped. Byte for byte what the launcher's
reference does (tools/map_merge/rules.py compose_reference; tests/test_enhanced_sections.py).

Header-only and plain C (standard library only): the server, the client and the offline tests share it.
*/
#ifndef COF_SECTIONS_H
#define COF_SECTIONS_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined( _MSC_VER )
#define COF_SECT_FN static __inline
#else
#define COF_SECT_FN static inline
#endif

#define COF_SECT_MAX       4
#define COF_SECT_NAME      64
#define COF_SECT_RENAMES   512
#define COF_SECT_STYLES    32
#define COF_SECT_LANDMARKS 64
#define COF_SECT_PAIRS     512

typedef struct
{
	char from[COF_SECT_NAME];
	char to[COF_SECT_NAME];
} cof_sect_rename_t;

typedef struct
{
	char   map[COF_SECT_NAME];       // the original map this section was
	double dz;                       // moved up by this much
	double zmin, zmax;               // its height band in the merged map
	int    first_model, models;      // its brush models *1.. are *first_model.. here
	int    num_renames;
	cof_sect_rename_t renames[COF_SECT_RENAMES];
	int    num_styles;
	int    style_from[COF_SECT_STYLES], style_to[COF_SECT_STYLES];
	int    num_landmarks;
	char   landmarks[COF_SECT_LANDMARKS][COF_SECT_NAME];   // its info_landmark names, after renames
} cof_section_t;

typedef struct
{
	char name[COF_SECT_NAME];        // the merged map file
	int  count;
	cof_section_t sec[COF_SECT_MAX];
} cof_sections_t;

COF_SECT_FN int CoF_SectCaseEq( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
		if( tolower( (unsigned char)*a ) != tolower( (unsigned char)*b )) return 0;
	return *a == *b;
}

// whitespace-separated words of one line; -1 when a word is too long or there are too many
COF_SECT_FN int CoF_SectSplit( const char *p, const char *end, char words[][COF_SECT_NAME], int max )
{
	int n = 0;
	for( ;; )
	{
		size_t len = 0;
		while( p < end && ( *p == ' ' || *p == '\t' || *p == '\r' )) p++;
		if( p >= end ) return n;
		if( n == max ) return -1;
		while( p < end && *p != ' ' && *p != '\t' && *p != '\r' )
		{
			if( len + 1 >= COF_SECT_NAME ) return -1;
			words[n][len++] = *p++;
		}
		words[n][len] = 0;
		n++;
	}
}

COF_SECT_FN int CoF_SectNumber( const char *s, double *out )
{
	char *end;
	double v = strtod( s, &end );
	if( end == s || *end ) return 0;
	*out = v;
	return 1;
}

COF_SECT_FN int CoF_SectInt( const char *s, int *out )
{
	char *end;
	long v = strtol( s, &end, 10 );
	if( end == s || *end || v < -1000000L || v > 1000000L ) return 0;
	*out = (int)v;
	return 1;
}

// 1 and the rules in out, or 0 for anything that is not a well-formed .sections file
COF_SECT_FN int CoF_SectionsParse( const char *text, cof_sections_t *out )
{
	const char *p = text;
	int first = 1;
	memset( out, 0, sizeof( *out ));
	while( *p )
	{
		char w[6][COF_SECT_NAME];
		const char *end = p;
		int n;
		while( *end && *end != '\n' ) end++;
		n = CoF_SectSplit( p, end, w, 6 );
		p = *end ? end + 1 : end;
		if( n < 0 ) return 0;
		if( n == 0 || w[0][0] == '#' ) continue;
		if( first )
		{
			if( n != 3 || strcmp( w[0], "cof-sections" ) || strcmp( w[1], "1" )) return 0;
			strcpy( out->name, w[2] );
			first = 0;
			continue;
		}
		if( !strcmp( w[0], "section" ))
		{
			cof_section_t *x;
			if( n != 5 || out->count == COF_SECT_MAX ) return 0;
			x = &out->sec[out->count];
			strcpy( x->map, w[1] );
			if( !CoF_SectNumber( w[2], &x->dz ) || !CoF_SectInt( w[3], &x->first_model ) || !CoF_SectInt( w[4], &x->models ))
				return 0;
			out->count++;
			continue;
		}
		if( !out->count ) return 0;
		{
			cof_section_t *x = &out->sec[out->count - 1];
			if( !strcmp( w[0], "bounds" ) && n == 3 )
			{
				if( !CoF_SectNumber( w[1], &x->zmin ) || !CoF_SectNumber( w[2], &x->zmax )) return 0;
			}
			else if( !strcmp( w[0], "rename" ) && n == 3 )
			{
				if( x->num_renames == COF_SECT_RENAMES ) return 0;
				strcpy( x->renames[x->num_renames].from, w[1] );
				strcpy( x->renames[x->num_renames].to, w[2] );
				x->num_renames++;
			}
			else if( !strcmp( w[0], "style" ) && n == 3 )
			{
				if( x->num_styles == COF_SECT_STYLES || !CoF_SectInt( w[1], &x->style_from[x->num_styles] )
					|| !CoF_SectInt( w[2], &x->style_to[x->num_styles] ))
					return 0;
				x->num_styles++;
			}
			else if( !strcmp( w[0], "landmark" ) && n == 2 )
			{
				if( x->num_landmarks == COF_SECT_LANDMARKS ) return 0;
				strcpy( x->landmarks[x->num_landmarks++], w[1] );
			}
			else return 0;
		}
	}
	return !first && out->count > 0;
}

// the section that was this original map, or -1
COF_SECT_FN int CoF_SectionsFind( const cof_sections_t *s, const char *map )
{
	int i;
	for( i = 0; i < s->count; i++ )
		if( CoF_SectCaseEq( s->sec[i].map, map )) return i;
	return -1;
}

// a name as it is in this section of the merged map
COF_SECT_FN const char *CoF_SectionsRename( const cof_section_t *x, const char *name )
{
	int i;
	for( i = 0; i < x->num_renames; i++ )
		if( !strcmp( x->renames[i].from, name )) return x->renames[i].to;
	return name;
}

COF_SECT_FN int CoF_SectionsStyle( const cof_section_t *x, int style )
{
	int i;
	for( i = 0; i < x->num_styles; i++ )
		if( x->style_from[i] == style ) return x->style_to[i];
	return style;
}

// the section whose height band holds z (or is nearest to it)
COF_SECT_FN int CoF_SectionsAt( const cof_sections_t *s, double z )
{
	int i, best = 0;
	double best_d = 1e30;
	for( i = 0; i < s->count; i++ )
	{
		const cof_section_t *x = &s->sec[i];
		double d = z < x->zmin ? x->zmin - z : z > x->zmax ? z - x->zmax : 0.0;
		if( d < best_d )
		{
			best_d = d;
			best = i;
		}
	}
	return best;
}

COF_SECT_FN int CoF_SectionsHasLandmark( const cof_section_t *x, const char *name )
{
	int i;
	for( i = 0; i < x->num_landmarks; i++ )
		if( !strcmp( x->landmarks[i], name )) return 1;
	return 0;
}

// the section whose landmarks include this (renamed) name, or -1
COF_SECT_FN int CoF_SectionsLandmarkSection( const cof_sections_t *s, const char *name )
{
	int i;
	for( i = 0; i < s->count; i++ )
		if( CoF_SectionsHasLandmark( &s->sec[i], name )) return i;
	return -1;
}

// a name as the original map had it (the reverse of CoF_SectionsRename)
COF_SECT_FN const char *CoF_SectionsOriginalName( const cof_section_t *x, const char *name )
{
	int i;
	for( i = 0; i < x->num_renames; i++ )
		if( !strcmp( x->renames[i].to, name )) return x->renames[i].from;
	return name;
}

typedef struct
{
	int  section_move;                    // 1: the destination is another section of the loaded file
	int  to_section;                      // the destination's section, -1 for a map that is not merged
	char file[COF_SECT_NAME];             // the map file to load (the loaded one for a section move)
	char dest_landmark[COF_SECT_NAME];    // the landmark's name in the destination
	char source_landmark[COF_SECT_NAME];  // its name where the request came from (the live entity)
} cof_sect_route_t;

/*
Where a change-level request (an original map name and the landmark as the requesting trigger names it) goes.
loaded: the loaded file's sections, NULL when it is not merged. dest: the sections of the merged file that holds
level, NULL when level is in none or merging is off. Landmark names go back to the original name in the requesting
section, then to the destination section's name for it.
*/
COF_SECT_FN void CoF_SectionsRoute( const cof_sections_t *loaded, const char *level, const char *landmark,
	const cof_sections_t *dest, cof_sect_route_t *out )
{
	const char *original = landmark ? landmark : "";
	int from = -1, to;
	memset( out, 0, sizeof( *out ));
	out->to_section = -1;
	snprintf( out->source_landmark, sizeof( out->source_landmark ), "%s", original );
	if( loaded && original[0] && ( from = CoF_SectionsLandmarkSection( loaded, original )) >= 0 )
		original = CoF_SectionsOriginalName( &loaded->sec[from], original );
	if( loaded && ( to = CoF_SectionsFind( loaded, level )) >= 0 )
	{
		out->section_move = 1;
		dest = loaded;
	}
	else if( !dest || ( to = CoF_SectionsFind( dest, level )) < 0 )
	{
		snprintf( out->file, sizeof( out->file ), "%s", level );
		snprintf( out->dest_landmark, sizeof( out->dest_landmark ), "%s", original );
		return;
	}
	out->to_section = to;
	snprintf( out->file, sizeof( out->file ), "%s", dest->name );
	snprintf( out->dest_landmark, sizeof( out->dest_landmark ), "%s", CoF_SectionsRename( &dest->sec[to], original ));
}

typedef struct
{
	int   alive, human, in_exit, moved_recently;
	float origin[3];
} cof_sect_player_t;

/*
Who a section move takes (movers[i] = 1): every living person in the exit who did not just move, and every
living person within reach of one of them. Nobody in the exit (a scripted change, say a cutscene's end): every
living person who did not just move. Someone in the exit whose touch does not count yet (moved_recently: just
moved, or standing in it since arriving): nobody moves (1.1.8; before, that read as a scripted change and took
everyone else). Bots follow their leader on their own; the dead respawn where they are. Returns how many move.
*/
COF_SECT_FN int CoF_SectionMovers( const cof_sect_player_t *p, int count, int *movers, float reach )
{
	int i, j, activators = 0, occupants = 0, moved = 0;
	for( i = 0; i < count; i++ )
	{
		movers[i] = p[i].alive && p[i].human && !p[i].moved_recently && p[i].in_exit;
		activators += movers[i];
		occupants += p[i].alive && p[i].human && p[i].in_exit;
	}
	if( !activators && occupants ) return 0;
	for( i = 0; i < count; i++ )
	{
		if( movers[i] || !p[i].alive || !p[i].human || p[i].moved_recently ) continue;
		if( !activators )
		{
			movers[i] = 1;
			continue;
		}
		for( j = 0; j < count; j++ )
		{
			float dx, dy, dz;
			if( !( p[j].alive && p[j].human && !p[j].moved_recently && p[j].in_exit )) continue;
			dx = p[i].origin[0] - p[j].origin[0];
			dy = p[i].origin[1] - p[j].origin[1];
			dz = p[i].origin[2] - p[j].origin[2];
			if( dx * dx + dy * dy + dz * dz <= reach * reach )
			{
				movers[i] = 1;
				break;
			}
		}
	}
	for( i = 0; i < count; i++ ) moved += movers[i];
	return moved;
}

// The session record (merged.txt in a story save): "merged 0|1" and "section <original map>". Its length, 0 when
// it does not fit.
COF_SECT_FN size_t CoF_SectionsRecord( char *out, size_t size, int merged, const char *section )
{
	int n = snprintf( out, size, "merged %d\nsection %s\n", merged ? 1 : 0, section && section[0] ? section : "-" );
	return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

// 1 and the values, or 0 for a missing or malformed record (a save from before merged maps: unmerged)
COF_SECT_FN int CoF_SectionsRecordParse( const char *text, size_t length, int *merged, char *section, size_t size )
{
	char buf[256], w[6][COF_SECT_NAME];
	const char *p, *end;
	int have_merged = 0, have_section = 0, n;
	*merged = 0;
	if( !text || !length || length >= sizeof( buf )) return 0;
	memcpy( buf, text, length );
	buf[length] = 0;
	for( p = buf; *p; )
	{
		for( end = p; *end && *end != '\n'; end++ ) ;
		n = CoF_SectSplit( p, end, w, 6 );
		p = *end ? end + 1 : end;
		if( n < 0 )
		{
			*merged = 0;
			return 0;
		}
		if( n == 0 ) continue;
		if( n == 2 && !strcmp( w[0], "merged" ) && ( !strcmp( w[1], "0" ) || !strcmp( w[1], "1" )))
		{
			*merged = w[1][0] == '1';
			have_merged = 1;
		}
		else if( n == 2 && !strcmp( w[0], "section" ))
		{
			snprintf( section, size, "%s", w[1] );
			have_section = 1;
		}
		else
		{
			*merged = 0;
			return 0;
		}
	}
	if( !have_merged || !have_section )
	{
		*merged = 0;
		return 0;
	}
	return 1;
}

// ---- entity composition ----

typedef struct
{
	char  *data;
	size_t len, cap;
	int    failed;
} cof_sect_buf_t;

typedef struct
{
	const char *k, *v;
	size_t kl, vl;
} cof_sect_pair_t;

COF_SECT_FN void CoF_SectPut( cof_sect_buf_t *b, const char *s, size_t n )
{
	if( b->failed ) return;
	if( b->len + n + 1 > b->cap )
	{
		size_t cap = b->cap ? b->cap : 65536;
		char *data;
		while( b->len + n + 1 > cap ) cap *= 2;
		data = (char *)realloc( b->data, cap );
		if( !data )
		{
			b->failed = 1;
			return;
		}
		b->data = data;
		b->cap = cap;
	}
	memcpy( b->data + b->len, s, n );
	b->len += n;
	b->data[b->len] = 0;
}

COF_SECT_FN void CoF_SectPutStr( cof_sect_buf_t *b, const char *s )
{
	CoF_SectPut( b, s, strlen( s ));
}

// the next entity: the text between a '{' and the next '}' with no '{' between them, braces inside a quoted value
// being the value's, as in the engine's tokenizer (decals are named like "{ding10": split there, a decal lost every
// key, and merged maps logged "Decal has invalid texture!"); as bsp.py entity_blocks
COF_SECT_FN int CoF_SectNextBlock( const char **cursor, const char **start, const char **stop )
{
	const char *p = *cursor;
	while( *p )
	{
		const char *q;
		if( *p == '"' )
		{
			for( p++; *p && *p != '"'; p++ ) ;
			if( *p ) p++;
			continue;
		}
		if( *p != '{' )
		{
			p++;
			continue;
		}
		for( q = p + 1; *q && *q != '{' && *q != '}'; q++ )
		{
			if( *q != '"' ) continue;
			for( q++; *q && *q != '"'; q++ ) ;
			if( !*q ) return 0;
		}
		if( !*q ) return 0;
		if( *q == '{' )
		{
			p = q;
			continue;
		}
		*start = p + 1;
		*stop = q;
		*cursor = q + 1;
		return 1;
	}
	return 0;
}

// the "key" "value" pairs of one entity; -1 when there are too many
COF_SECT_FN int CoF_SectPairs( const char *p, const char *stop, cof_sect_pair_t *pairs, int max )
{
	int n = 0;
	while( p < stop )
	{
		const char *k, *ke, *v, *ve, *q;
		if( *p != '"' )
		{
			p++;
			continue;
		}
		k = p + 1;
		for( ke = k; ke < stop && *ke != '"'; ke++ ) ;
		if( ke >= stop ) break;
		for( q = ke + 1; q < stop && isspace( (unsigned char)*q ); q++ ) ;
		if( q >= stop || *q != '"' )
		{
			p++;
			continue;
		}
		v = q + 1;
		for( ve = v; ve < stop && *ve != '"'; ve++ ) ;
		if( ve >= stop )
		{
			p++;
			continue;
		}
		if( n == max ) return -1;
		pairs[n].k = k;
		pairs[n].kl = (size_t)( ke - k );
		pairs[n].v = v;
		pairs[n].vl = (size_t)( ve - v );
		n++;
		p = ve + 1;
	}
	return n;
}

COF_SECT_FN int CoF_SectIs( const char *s, size_t n, const char *word )
{
	return strlen( word ) == n && !memcmp( s, word, n );
}

COF_SECT_FN int CoF_SectInList( const char *s, size_t n, const char *const *list, size_t count )
{
	size_t i;
	for( i = 0; i < count; i++ )
		if( CoF_SectIs( s, n, list[i] )) return 1;
	return 0;
}

// Keys whose value names an entity, whatever the class, and those that do only for a class: the merger's rules
// (tools/map_merge/rules.py REF_KEYS, CLASS_REF_KEYS in the launcher repo; a test compares them).
static const char *const cof_sect_ref_keys[] = {
	"target", "killtarget", "targetname", "landmark", "changetarget", "master", "netname", "parentname",
	"LightningStart", "LightningEnd", "m_iszEntity", "TriggerTarget", "movewith", "moveto", "m_iszAltTarget",
	"m_iszBothTarget", "m_iszThreadName", "m_iszLocusThread", "m_iszWatch", "m_iszPath", "m_iszNewTarget",
	"m_iszViewEntity", "skipmanager", "m_sAttachedModel", "trigger_one", "trigger_two", "trigger_three", "trigger_four",
	"triggerone", "triggertwo", "triggerthree", "triggerfour", "triggerfive", "notrigger", "cameraent", "simonent",
	"player1trigger", "player2trigger", "player3trigger", "player4trigger" };
static const char *const cof_sect_class_keys[][2] =
{
	{ "path_corner", "message" }, { "path_track", "message" }, { "trigger_teleport", "message" },
	{ "trigger_teleport", "noise" }, { "trigger_onsight", "message" }, { "trigger_onsight", "noise" },
	{ "trigger_onsight", "noise1" }, { "trigger_camera", "message" }, { "trigger_playercheckarea", "message" },
	{ "trigger_booksimon", "message" }, { "scripted_startrush", "message" }, { "env_sprite", "message" },
	{ "watcher_count", "message" }, { "watcher_count", "noise" }, { "watcher_count", "noise1" },
	{ "watcher_count", "noise2" }, { "cof_teleport", "message" }, { "cof_interdooronoff", "message" },
	{ "cof_updatekeypad", "message" }, { "cof_bosshealthbar", "message" }, { "cof_doctorweapontrigger", "message" },
	{ "cof_mdlcutscene", "message" }, { "cof_wheelchairmode", "message" }, { "cof_fatfuckingcunt", "message" },
	{ "cof_spawnpointonoff", "message" }, { "cof_lobby_start", "message" }, { "cof_survivalmode", "message" },
	{ "cof_randomtimedspawner", "message" }, { "cof_weapontrigger", "noise" }, { "cof_skipmanager", "entity" },
	{ "cof_skipmanager", "teleport" }, { "cof_ladder_manager", "topent" }, { "cof_ladder_manager", "bottoment" },
	{ "inter_door", "propertarget" }, { "inter_door", "useunlock" }, { "inter_door_coop", "propertarget" },
	{ "inter_door_coop", "useunlock" }
};

COF_SECT_FN int CoF_SectClassKey( const char *cls, size_t cls_len, const char *k, size_t kl )
{
	size_t i;
	for( i = 0; i < sizeof( cof_sect_class_keys ) / sizeof( cof_sect_class_keys[0] ); i++ )
		if( CoF_SectIs( cls, cls_len, cof_sect_class_keys[i][0] ) && CoF_SectIs( k, kl, cof_sect_class_keys[i][1] )) return 1;
	return 0;
}
static const char *const cof_sect_mm_reserved[] = { "classname", "targetname", "origin", "angles", "spawnflags", "wait",
	"delay", "target", "killtarget", "master", "globalname", "rendermode", "renderamt", "rendercolor", "renderfx", "model" };

COF_SECT_FN const char *CoF_SectRenameN( const cof_section_t *x, const char *s, size_t n )
{
	int i;
	for( i = 0; i < x->num_renames; i++ )
		if( CoF_SectIs( s, n, x->renames[i].from )) return x->renames[i].to;
	return NULL;
}

COF_SECT_FN void CoF_SectQuoted( cof_sect_buf_t *b, const char *s, size_t n )
{
	CoF_SectPut( b, "\"", 1 );
	CoF_SectPut( b, s, n );
	CoF_SectPut( b, "\"", 1 );
}

// a coordinate as the launcher's reference writes it: up to 6 decimals, trailing zeros dropped
COF_SECT_FN void CoF_SectCoordinate( double v, char *out, size_t size )
{
	char *e;
	snprintf( out, size, "%.6f", v );
	e = out + strlen( out );
	while( e > out && e[-1] == '0' ) *--e = 0;
	if( e > out && e[-1] == '.' ) *--e = 0;
	if( !out[0] || !strcmp( out, "-0" )) strcpy( out, "0" );
}

// "x y z" moved up by dz, or 0 when the value is not three words with a number last
COF_SECT_FN int CoF_SectMovedOrigin( const char *v, size_t n, double dz, char *out, size_t size )
{
	const char *w[4], *we[4], *p = v, *end = v + n;
	char z[64], *zend, coord[64];
	int count = 0;
	double value;
	while( p < end )
	{
		while( p < end && isspace( (unsigned char)*p )) p++;
		if( p >= end ) break;
		if( count == 3 ) return 0;
		w[count] = p;
		while( p < end && !isspace( (unsigned char)*p )) p++;
		we[count++] = p;
	}
	if( count != 3 || (size_t)( we[2] - w[2] ) >= sizeof( z )) return 0;
	memcpy( z, w[2], (size_t)( we[2] - w[2] ));
	z[we[2] - w[2]] = 0;
	value = strtod( z, &zend );
	if( zend == z || *zend ) return 0;
	CoF_SectCoordinate( value + dz, coord, sizeof( coord ));
	snprintf( out, size, "%.*s %.*s %s", (int)( we[0] - w[0] ), w[0], (int)( we[1] - w[1] ), w[1], coord );
	return 1;
}

COF_SECT_FN int CoF_SectDigits( const char *s, size_t n )
{
	size_t i;
	if( !n ) return 0;
	for( i = 0; i < n; i++ )
		if( !isdigit( (unsigned char)s[i] )) return 0;
	return 1;
}

COF_SECT_FN void CoF_SectEntity( cof_sect_buf_t *b, const cof_sect_pair_t *pairs, int n, const cof_section_t *x )
{
	const char *cls = "";
	size_t cls_len = 0;
	char value[256];
	int i;
	for( i = 0; i < n; i++ )
		if( CoF_SectIs( pairs[i].k, pairs[i].kl, "classname" ))
		{
			cls = pairs[i].v;
			cls_len = pairs[i].vl;
			break;
		}
	CoF_SectPutStr( b, "{\n" );
	for( i = 0; i < n; i++ )
	{
		const char *k = pairs[i].k, *v = pairs[i].v, *renamed;
		size_t kl = pairs[i].kl, vl = pairs[i].vl;
		const char *key_tail = NULL;
		size_t tail_len = 0;
		if( CoF_SectIs( k, kl, "origin" ) && x->dz != 0.0 )
		{
			if( CoF_SectMovedOrigin( v, vl, x->dz, value, sizeof( value )))
			{
				v = value;
				vl = strlen( value );
			}
		}
		else if( CoF_SectIs( k, kl, "model" ) && vl > 1 && v[0] == '*' && CoF_SectDigits( v + 1, vl - 1 ) && atoi( v + 1 ) >= 1 )
		{
			snprintf( value, sizeof( value ), "*%d", atoi( v + 1 ) - 1 + x->first_model );
			v = value;
			vl = strlen( value );
		}
		else if( CoF_SectInList( k, kl, cof_sect_ref_keys, sizeof( cof_sect_ref_keys ) / sizeof( cof_sect_ref_keys[0] ))
			|| CoF_SectClassKey( cls, cls_len, k, kl ))
		{
			if(( renamed = CoF_SectRenameN( x, v, vl )) != NULL )
			{
				v = renamed;
				vl = strlen( renamed );
			}
		}
		else if( CoF_SectIs( k, kl, "style" ) && cls_len >= 5 && !memcmp( cls, "light", 5 )
			&& CoF_SectDigits( v[0] == '-' ? v + 1 : v, v[0] == '-' ? vl - 1 : vl ))
		{
			char digits[32];
			if( vl < sizeof( digits ))
			{
				memcpy( digits, v, vl );
				digits[vl] = 0;
				snprintf( value, sizeof( value ), "%d", CoF_SectionsStyle( x, atoi( digits )));
				v = value;
				vl = strlen( value );
			}
		}
		else if( CoF_SectIs( cls, cls_len, "multi_manager" )
			&& !CoF_SectInList( k, kl, cof_sect_mm_reserved, sizeof( cof_sect_mm_reserved ) / sizeof( cof_sect_mm_reserved[0] )))
		{
			size_t base = 0;
			while( base < kl && k[base] != '#' ) base++;
			if(( renamed = CoF_SectRenameN( x, k, base )) != NULL )
			{
				key_tail = k + base;
				tail_len = kl - base;
				k = renamed;
				kl = strlen( renamed );
			}
		}
		CoF_SectPut( b, "\"", 1 );
		CoF_SectPut( b, k, kl );
		if( key_tail ) CoF_SectPut( b, key_tail, tail_len );
		CoF_SectPut( b, "\" ", 2 );
		CoF_SectQuoted( b, v, vl );
		CoF_SectPut( b, "\n", 1 );
	}
	CoF_SectPutStr( b, "}\n" );
}

/*
The merged map's entity string: the merged file's own worldspawn (world_lump's first entity), then every member's
entities but its worldspawn, through that member's rules, then the merged file's own other entities unchanged (those
the merger adds, such as c_start's easter egg, which hl.dll spawns only in a map of that name). members[i] is member
i's entity string as the engine would load it for the original map. Returns a malloc'd string (its length in
*outlen), or NULL.
*/
COF_SECT_FN char *CoF_ComposeEntities( const char *world_lump, const char *const *members, int count, const cof_sections_t *s,
	size_t *outlen )
{
	static cof_sect_pair_t pairs[COF_SECT_PAIRS];
	static cof_section_t none;   // no move, no renames: the merged file's own entities as written
	cof_sect_buf_t b = { NULL, 0, 0, 0 };
	const char *cursor = world_lump, *start, *stop, *own;
	int i, n;
	if( !world_lump || count != s->count ) return NULL;
	if( !CoF_SectNextBlock( &cursor, &start, &stop ) || ( n = CoF_SectPairs( start, stop, pairs, COF_SECT_PAIRS )) < 0 )
		return NULL;
	own = cursor;
	memset( &none, 0, sizeof( none ));
	none.first_model = 1;
	CoF_SectEntity( &b, pairs, n, &none );
	for( i = 0; i < count; i++ )
	{
		cursor = members[i];
		if( !cursor ) break;
		while( CoF_SectNextBlock( &cursor, &start, &stop ))
		{
			int k, world = 0;
			if(( n = CoF_SectPairs( start, stop, pairs, COF_SECT_PAIRS )) < 0 )
			{
				b.failed = 1;
				break;
			}
			for( k = 0; k < n; k++ )
				if( CoF_SectIs( pairs[k].k, pairs[k].kl, "classname" ))
				{
					world = CoF_SectIs( pairs[k].v, pairs[k].vl, "worldspawn" );
					break;
				}
			if( !world ) CoF_SectEntity( &b, pairs, n, &s->sec[i] );
		}
	}
	if( i == count )
		while( CoF_SectNextBlock( &own, &start, &stop ))
		{
			if(( n = CoF_SectPairs( start, stop, pairs, COF_SECT_PAIRS )) < 0 )
			{
				b.failed = 1;
				break;
			}
			CoF_SectEntity( &b, pairs, n, &none );
		}
	if( i < count || b.failed || !b.data )
	{
		free( b.data );
		return NULL;
	}
	if( outlen ) *outlen = b.len;
	return b.data;
}

// The view distance a member map's own worldspawn gives: hl.dll's CWorld takes MaxRange with atof and sets sv_zmax to
// it, or to 4096 when it is not above 0 (world.cpp:1004-1008, 923-926 in the decompilation).
#define COF_SECT_DEFAULT_RANGE 4096.0
COF_SECT_FN double CoF_SectMaxRange( const char *ents )
{
	static cof_sect_pair_t pairs[COF_SECT_PAIRS];
	const char *cursor = ents, *start, *stop;
	char text[64];
	int n, k;
	if( !ents || !CoF_SectNextBlock( &cursor, &start, &stop ) || ( n = CoF_SectPairs( start, stop, pairs, COF_SECT_PAIRS )) < 0 )
		return COF_SECT_DEFAULT_RANGE;
	for( k = 0; k < n; k++ )
	{
		double value;
		if( !CoF_SectIs( pairs[k].k, pairs[k].kl, "MaxRange" ) || pairs[k].vl >= sizeof( text )) continue;
		memcpy( text, pairs[k].v, pairs[k].vl );
		text[pairs[k].vl] = 0;
		value = atof( text );
		return value > 0.0 ? value : COF_SECT_DEFAULT_RANGE;
	}
	return COF_SECT_DEFAULT_RANGE;
}

#endif // COF_SECTIONS_H
