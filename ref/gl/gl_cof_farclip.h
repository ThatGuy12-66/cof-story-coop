/*
gl_cof_farclip.h - Cry of Fear: the world view's far distance (gl_rmain.c R_GetFarClip)
GPL-3.0-or-later.

Cry of Fear's own engine projects to exactly movevars.zmax (worldspawn MaxRange, sv_zmax; hw.dll R_SetupGL), and
client.dll's renderer draws inside that projection with no far plane of its own. Xash used zmax * 1.73, so every map
showed 73 % further than the original, including the forests whose darkness is their short MaxRange
(docs/research/2026-10-18-cof-decompilation-survey.md 1.3, the launcher repo). cof_farclip_scale: 1 = retail.

Plain C: the offline tests share it.
*/
#ifndef GL_COF_FARCLIP_H
#define GL_COF_FARCLIP_H

#if defined( _MSC_VER )
#define COF_FARCLIP_FN static __inline
#else
#define COF_FARCLIP_FN static inline
#endif

COF_FARCLIP_FN float CoF_FarClip( float zmax, float scale )
{
	return zmax * ( scale > 0.0f ? scale : 1.0f );
}

#endif // GL_COF_FARCLIP_H
