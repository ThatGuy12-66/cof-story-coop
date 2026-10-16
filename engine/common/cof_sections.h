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

static int CoF_SectCaseEq( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
		if( tolower( (unsigned char)*a ) != tolower( (unsigned char)*b )) return 0;
	return *a == *b;
}

// whitespace-separated words of one line; -1 when a word is too long or there are too many
static int CoF_SectSplit( const char *p, const char *end, char words[][COF_SECT_NAME], int max )
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

static int CoF_SectNumber( const char *s, double *out )
{
	char *end;
	double v = strtod( s, &end );
	if( end == s || *end ) return 0;
	*out = v;
	return 1;
}

static int CoF_SectInt( const char *s, int *out )
{
	char *end;
	long v = strtol( s, &end, 10 );
	if( end == s || *end || v < -1000000L || v > 1000000L ) return 0;
	*out = (int)v;
	return 1;
}

// 1 and the rules in out, or 0 for anything that is not a well-formed .sections file
static int CoF_SectionsParse( const char *text, cof_sections_t *out )
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
static int CoF_SectionsFind( const cof_sections_t *s, const char *map )
{
	int i;
	for( i = 0; i < s->count; i++ )
		if( CoF_SectCaseEq( s->sec[i].map, map )) return i;
	return -1;
}

// a name as it is in this section of the merged map
static const char *CoF_SectionsRename( const cof_section_t *x, const char *name )
{
	int i;
	for( i = 0; i < x->num_renames; i++ )
		if( !strcmp( x->renames[i].from, name )) return x->renames[i].to;
	return name;
}

static int CoF_SectionsStyle( const cof_section_t *x, int style )
{
	int i;
	for( i = 0; i < x->num_styles; i++ )
		if( x->style_from[i] == style ) return x->style_to[i];
	return style;
}

// the section whose height band holds z (or is nearest to it)
static int CoF_SectionsAt( const cof_sections_t *s, double z )
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

static int CoF_SectionsHasLandmark( const cof_section_t *x, const char *name )
{
	int i;
	for( i = 0; i < x->num_landmarks; i++ )
		if( !strcmp( x->landmarks[i], name )) return 1;
	return 0;
}

// the section whose landmarks include this (renamed) name, or -1
static int CoF_SectionsLandmarkSection( const cof_sections_t *s, const char *name )
{
	int i;
	for( i = 0; i < s->count; i++ )
		if( CoF_SectionsHasLandmark( &s->sec[i], name )) return i;
	return -1;
}

// a name as the original map had it (the reverse of CoF_SectionsRename)
static const char *CoF_SectionsOriginalName( const cof_section_t *x, const char *name )
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
static void CoF_SectionsRoute( const cof_sections_t *loaded, const char *level, const char *landmark,
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

static void CoF_SectPut( cof_sect_buf_t *b, const char *s, size_t n )
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

static void CoF_SectPutStr( cof_sect_buf_t *b, const char *s )
{
	CoF_SectPut( b, s, strlen( s ));
}

// the next entity: the text between a '{' and the next '}' with no '{' between them (as bsp.py parse_ents)
static int CoF_SectNextBlock( const char **cursor, const char **start, const char **stop )
{
	const char *p = *cursor;
	while( *p )
	{
		const char *q;
		if( *p != '{' )
		{
			p++;
			continue;
		}
		q = p + 1;
		while( *q && *q != '{' && *q != '}' ) q++;
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
static int CoF_SectPairs( const char *p, const char *stop, cof_sect_pair_t *pairs, int max )
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

static int CoF_SectIs( const char *s, size_t n, const char *word )
{
	return strlen( word ) == n && !memcmp( s, word, n );
}

static int CoF_SectInList( const char *s, size_t n, const char *const *list, size_t count )
{
	size_t i;
	for( i = 0; i < count; i++ )
		if( CoF_SectIs( s, n, list[i] )) return 1;
	return 0;
}

static const char *const cof_sect_ref_keys[] = { "target", "killtarget", "targetname", "landmark", "changetarget", "master",
	"netname", "parentname", "LightningStart", "LightningEnd", "m_iszEntity", "TriggerTarget" };
static const char *const cof_sect_mm_reserved[] = { "classname", "targetname", "origin", "angles", "spawnflags", "wait",
	"delay", "target", "killtarget", "master", "globalname", "rendermode", "renderamt", "rendercolor", "renderfx", "model" };

static const char *CoF_SectRenameN( const cof_section_t *x, const char *s, size_t n )
{
	int i;
	for( i = 0; i < x->num_renames; i++ )
		if( CoF_SectIs( s, n, x->renames[i].from )) return x->renames[i].to;
	return NULL;
}

static void CoF_SectQuoted( cof_sect_buf_t *b, const char *s, size_t n )
{
	CoF_SectPut( b, "\"", 1 );
	CoF_SectPut( b, s, n );
	CoF_SectPut( b, "\"", 1 );
}

// a coordinate as the launcher's reference writes it: up to 6 decimals, trailing zeros dropped
static void CoF_SectCoordinate( double v, char *out, size_t size )
{
	char *e;
	snprintf( out, size, "%.6f", v );
	e = out + strlen( out );
	while( e > out && e[-1] == '0' ) *--e = 0;
	if( e > out && e[-1] == '.' ) *--e = 0;
	if( !out[0] || !strcmp( out, "-0" )) strcpy( out, "0" );
}

// "x y z" moved up by dz, or 0 when the value is not three words with a number last
static int CoF_SectMovedOrigin( const char *v, size_t n, double dz, char *out, size_t size )
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

static int CoF_SectDigits( const char *s, size_t n )
{
	size_t i;
	if( !n ) return 0;
	for( i = 0; i < n; i++ )
		if( !isdigit( (unsigned char)s[i] )) return 0;
	return 1;
}

static void CoF_SectEntity( cof_sect_buf_t *b, const cof_sect_pair_t *pairs, int n, const cof_section_t *x )
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
			|| ( CoF_SectIs( k, kl, "message" ) && ( CoF_SectIs( cls, cls_len, "path_corner" ) || CoF_SectIs( cls, cls_len, "path_track" ))))
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
entities but its worldspawn, through that member's rules. members[i] is member i's entity string as the engine
would load it for the original map. Returns a malloc'd string (its length in *outlen), or NULL.
*/
static char *CoF_ComposeEntities( const char *world_lump, const char *const *members, int count, const cof_sections_t *s,
	size_t *outlen )
{
	static cof_sect_pair_t pairs[COF_SECT_PAIRS];
	cof_sect_buf_t b = { NULL, 0, 0, 0 };
	const char *cursor = world_lump, *start, *stop;
	int i, n;
	if( !world_lump || count != s->count ) return NULL;
	if( !CoF_SectNextBlock( &cursor, &start, &stop ) || ( n = CoF_SectPairs( start, stop, pairs, COF_SECT_PAIRS )) < 0 )
		return NULL;
	{
		cof_section_t none;
		memset( &none, 0, sizeof( none ));
		none.first_model = 1;
		CoF_SectEntity( &b, pairs, n, &none );
	}
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
	if( i < count || b.failed || !b.data )
	{
		free( b.data );
		return NULL;
	}
	if( outlen ) *outlen = b.len;
	return b.data;
}

#endif // COF_SECTIONS_H
