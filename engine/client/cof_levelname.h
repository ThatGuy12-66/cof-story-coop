/*
cof_levelname.h - Cry of Fear story co-op: the level name client.dll's studio renderer gets in a merged map
GPL-3.0-or-later.

client.dll asks the engine for the level name ("maps/<name>.bsp", pfnGetLevelName in cl_game.c) and decides a few
things by it. In a merged map (maps/cm_*.bsp, cof_sections.h) that is the merged file's name. Every caller in the
retail client.dll (decomp cl_dll), and what the name means to it (1.1.9 review, R2):
- The studio renderer's prop distance (gl_studiomodel.cpp, GL_DrawPoints): renderfx 92 props are culled at 800
  units only in levels named college1dark or forest1 to forest6. cm_college2 lost it for its c_college1dark
  section, cm_bridge for c_forest1, and cm_forest6 gave it to c_forest7.
- The custom renderer's lightmaps (gl_lightmap.cpp, LightmapsNeedsUpdate, every frame): rebuilt for the whole world
  whenever the name changes. A name that followed the player's section would rebuild them at every section move,
  and a section named like a map later loaded unmerged would keep the old map's lightmaps on the new one.
- The rest test names no story section has (c_nightmare, game_menu, cof_, video_stress_test, c_loadgame,
  difficulty_, server_, unlockables), or look for per-map files at map start (maps/<map>_detail.txt, none shipped;
  textures/<map>/; overviews/<map>.txt).
So the name of the original map is given only while client.dll draws a studio model (the engine's call into its
StudioDrawModel): the prop distance is then the original level's, and every other caller keeps the merged name.

Plain C: the offline tests share it.
*/
#ifndef COF_LEVELNAME_H
#define COF_LEVELNAME_H

#include "cof_sections.h"

#if defined( _MSC_VER )
#define COF_LEVELNAME_FN static __inline
#else
#define COF_LEVELNAME_FN static inline
#endif

// The map name (without maps/ and .bsp) for client.dll's level-name question. map: the loaded file's name;
// sections: its rules when it is a merged map, else NULL; drawing_studio: client.dll is drawing a studio model;
// z: the local player's height then.
COF_LEVELNAME_FN const char *CoF_LevelName( const char *map, const cof_sections_t *sections, int drawing_studio, double z )
{
	if( !drawing_studio || !sections || sections->count <= 0 )
		return map;
	return sections->sec[CoF_SectionsAt( sections, z )].map;
}

#endif // COF_LEVELNAME_H
