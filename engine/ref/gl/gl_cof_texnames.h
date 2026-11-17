/*
gl_cof_texnames.h - Cry of Fear: an engine texture's GL name is its slot (gl_image.c GL_AllocTexture)
GPL-3.0-or-later.

client.dll's renderer binds the engine's texture numbers straight to GL, the way GoldSrc's numbers worked (studio
skins: glBindTexture( ptex->index & 0xFFFF ), sprite frames: frame->gl_texturenum), so an engine texture only shows
right when its GL name is its slot in gl_textures. client.dll also names its own textures without asking GL, counting
up from 1 << 25: lightmaps, the six sky faces, screen copies. A driver that offers the name after the highest one in
use hands the engine names in client.dll's range once client.dll has bound its first one: every texture client.dll
binds by number is then the wrong one, and the sky it draws shows whatever the engine uploaded into the sky's names
(one AMD Radeon RX 5500 player saw walls in the sky and grey walls in front of him).

When the driver offers a name that can't be a slot, the engine takes the lowest free slot's number instead, as
drivers that offer the lowest free name already do.

Plain C: the offline tests share it.
*/
#ifndef GL_COF_TEXNAMES_H
#define GL_COF_TEXNAMES_H

#if defined( _MSC_VER )
#define COF_TEXNAMES_FN static __inline
#else
#define COF_TEXNAMES_FN static inline
#endif

// is this name already a texture's (a slot holds it, or GL knows it)?
typedef int (*cof_name_taken_fn)( unsigned int name, void *ctx );

// the lowest name in 1 .. slots - 1, outside skip_lo .. skip_hi, that isn't taken; 0 when every one is
COF_TEXNAMES_FN unsigned int CoF_SlotTextureName( unsigned int slots, unsigned int skip_lo, unsigned int skip_hi,
	cof_name_taken_fn taken, void *ctx )
{
	unsigned int name;

	for( name = 1; name < slots; name++ )
	{
		if( name >= skip_lo && name <= skip_hi )
			continue;
		if( !taken( name, ctx ))
			return name;
	}
	return 0;
}

#endif // GL_COF_TEXNAMES_H
