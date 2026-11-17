/*
gl_cof_depthrange.h - Cry of Fear: the world's depth range (gl_rmain.c R_Clear, gl_studio.c R_DrawViewModel)
GPL-3.0-or-later.

Cry of Fear ships its own opengl32.dll (Paranoia's, "PARANOIA_HACKS_V1"). Its glDepthRange turns 0..1 and 0..0.5
into 0..0.8, so the engine draws the world in 0..0.8 and client.dll keeps the rest for what it draws after the
world: the 3D-sky entities in 0.8..0.9 and the sky box in 0.9..1 (gl_sky.cpp DrawSky), then its models back in
0..0.8. The engine only gets that glDepthRange when it asks the dll for it by name. SDL asks wglGetProcAddress
first, and a driver that answers it for OpenGL 1.1 functions hands the engine the driver's own glDepthRange: the
world then spans 0..1, so the sky box covers everything more than a few steps away and models show through walls
(an AMD Radeon RX 5500 player: walls in the sky, floating trees, a grey wall in front of him). The engine applies
the dll's rule itself. Where its calls already go through the dll, the dll gets 0..0.8 and leaves it alone.

Plain C: the offline tests share it.
*/
#ifndef GL_COF_DEPTHRANGE_H
#define GL_COF_DEPTHRANGE_H

#if defined( _MSC_VER )
#define COF_DEPTHRANGE_FN static __inline
#else
#define COF_DEPTHRANGE_FN static inline
#endif

#define COF_WORLD_DEPTH_FAR 0.8

// opengl32.dll's glDepthRange rule: from a near of 0, a far of 0.5 or 1 becomes 0.8; anything else stands
COF_DEPTHRANGE_FN double CoF_DepthRangeFar( double znear, double zfar )
{
	if( znear == 0.0 && ( zfar == 0.5 || zfar == 1.0 ))
		return COF_WORLD_DEPTH_FAR;
	return zfar;
}

#endif // GL_COF_DEPTHRANGE_H
