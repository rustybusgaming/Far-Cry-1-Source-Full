#ifndef _CRY_GLES_TEXTURE_H_
#define _CRY_GLES_TEXTURE_H_

/*!
	GLESTexture -- uploading the engine's textures to WebGL2.

	THE ID PROBLEM

	The engine hands textures around as plain integer ids and expects to be able
	to invent them (DownLoadToVideoMemory takes an Id parameter meaning "reuse
	this one"), so the backend cannot simply return GL names and hope they line
	up. This keeps a table from engine id to GL texture name, which also gives
	somewhere to record the size and format for the later calls that only pass
	an id.

	THE FORMAT PROBLEM

	The engine's eTF_8888 is, in its own header's words, "usually BGRA" -- the
	Direct3D byte order. GLES 3.0 can express that with texture swizzle
	parameters, but WebGL2 does not expose them, so BGRA data is reordered on
	the CPU at upload time. It is a real cost, paid once per texture rather than
	per frame, and it is why eTF_RGBA exists as a separate format: data already
	in the right order skips it.

	Compressed formats do not have that problem and must not be touched:
	DXT1/3/5 go straight to glCompressedTexImage2D. Far Cry's textures are
	almost entirely DXT, and Chromium reports WEBGL_compressed_texture_s3tc
	available, so the common case is a direct upload with no CPU work at all.
*/

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

//! Upload a texture and return the engine-side id, or 0 on failure.
//!
//! nId of 0 allocates a new id; a non-zero nId replaces the contents of an
//! existing one, which is what the engine does when a texture is reloaded.
unsigned int GLESTexture_Upload(unsigned int nId, const unsigned char* pData,
                                int nWidth, int nHeight, int eTF,
                                bool bRepeat, bool bMipmap);

//! Replace a sub-rectangle. Used for font atlases, which are built glyph by
//! glyph as characters are first seen.
void GLESTexture_UpdateRegion(unsigned int nId, const unsigned char* pData,
                              int nX, int nY, int nWidth, int nHeight, int eTF);

//! Bind for drawing, on texture unit 0. Passing 0 unbinds, which is what makes
//! the next draw untextured.
void GLESTexture_Bind(unsigned int nId);

//! Whether a texture is currently bound -- the drawing path asks this to decide
//! whether to sample.
bool GLESTexture_IsBound();

void GLESTexture_Remove(unsigned int nId);

//! Drop everything. The GL names mean nothing after a context loss.
void GLESTexture_Shutdown();

#endif //__EMSCRIPTEN__

#endif //_CRY_GLES_TEXTURE_H_
