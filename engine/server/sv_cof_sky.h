/*
sv_cof_sky.h - Cry of Fear story co-op: a merged map's 3D skies, each section its own (sv_cof_sky.c)
GPL-3.0-or-later.

Each envpos_sky / envpos_world sends its marker to a joining player (SendInitMessage 1009B9C0 / 1009BA80:
skymark_sky coord x, y, z, byte flags, [byte ambient], [byte shade]; skymark_w coord x, y, z, coord speed), and
client.dll keeps only the last one and draws every sky model (renderfx 70, 137 or 184, which AddToFullPack sends to
everyone without the PVS test) against it (gl_sky.cpp:88-124, gl_renderer.cpp:326-387, client.cpp:3113 in the
decompilation). In a merged map every section showed the last member's sky. Sections are stacked bands of z, so a
marker's section is that of its z. Research: docs/research/2026-10-18-cof-decompilation-survey.md section 5.2.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_SKY_H
#define SV_COF_SKY_H

#if defined( _MSC_VER )
#define COF_SKY_FN static __inline
#else
#define COF_SKY_FN static inline
#endif

#define COF_SKY_PAYLOAD_MAX 16

COF_SKY_FN int CoF_SkyModel( int renderfx )
{
	return renderfx == 70 || renderfx == 137 || renderfx == 184;
}

// A sky model kept from a viewer: both in sections of a merged map, different ones.
COF_SKY_FN int CoF_SkyHidden( int renderfx, int viewer_section, int model_section )
{
	return CoF_SkyModel( renderfx ) && viewer_section >= 0 && model_section >= 0 && viewer_section != model_section;
}

// 0 skymark_sky, 1 skymark_w, -1 another message.
COF_SKY_FN int CoF_SkyMarker( const char *name )
{
	if( !name ) return -1;
	if( !strcmp( name, "skymark_sky" )) return 0;
	if( !strcmp( name, "skymark_w" )) return 1;
	return -1;
}

// The z of a marker (its third coord: 1/8 units, or whole ones with large coords).
COF_SKY_FN int CoF_SkyMarkerHeight( const unsigned char *payload, int size, int large, float *z )
{
	short value;
	if( !payload || size < 6 ) return 0;
	value = (short)( payload[4] | ( payload[5] << 8 ));
	*z = large ? (float)value : value / 8.0f;
	return 1;
}

#endif // SV_COF_SKY_H
