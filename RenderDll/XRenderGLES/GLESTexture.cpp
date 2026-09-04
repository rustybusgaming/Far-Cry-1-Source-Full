////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESTexture.cpp
//  Description: Texture upload and binding. See GLESTexture.h.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESTexture.h"

#if defined(__EMSCRIPTEN__)

#include <stdlib.h>
#include <string.h>
#include <vector>

//! The S3TC enums come from WEBGL_compressed_texture_s3tc, which GLESContext
//! enables at startup. They are not in gl3.h, so they are spelled out rather
//! than pulled in from an extension header that may or may not be present.
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
#endif

struct SGLESTexture
{
	GLuint	nName;
	int		nWidth;
	int		nHeight;
	int		eTF;
	bool	bUsed;
};

//! Index 0 is never handed out: the engine treats a texture id of 0 as "none".
static std::vector<SGLESTexture>	g_textures;
static unsigned int					g_nBound = 0;

//////////////////////////////////////////////////////////////////////////

static bool IsCompressed(int eTF)
{
	return eTF == eTF_DXT1 || eTF == eTF_DXT3 || eTF == eTF_DXT5;
}

static GLenum CompressedFormat(int eTF)
{
	switch (eTF)
	{
	case eTF_DXT1:	return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	case eTF_DXT3:	return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
	case eTF_DXT5:	return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	default:		return 0;
	}
}

//! DXT stores 4x4 blocks: 8 bytes per block for DXT1, 16 for DXT3/5. Dimensions
//! round up, so a 2x2 texture still costs a whole block.
static int CompressedSize(int eTF, int nWidth, int nHeight)
{
	const int nBlocks = ((nWidth + 3) / 4) * ((nHeight + 3) / 4);
	return nBlocks * (eTF == eTF_DXT1 ? 8 : 16);
}

//////////////////////////////////////////////////////////////////////////
//! Reorder BGRA to RGBA.
//!
//! GLES 3.0 could do this with GL_TEXTURE_SWIZZLE_B/R, but WebGL2 does not
//! expose those parameters, so it happens here. Once per upload rather than
//! per frame, and skipped entirely for eTF_RGBA and for compressed data.
//////////////////////////////////////////////////////////////////////////
static unsigned char* SwizzleBGRAtoRGBA(const unsigned char* pData, int nPixels,
                                        bool bForceOpaque)
{
	unsigned char* pOut = (unsigned char*)malloc((size_t)nPixels * 4);
	if (!pOut)
		return 0;

	for (int i = 0; i < nPixels; ++i)
	{
		pOut[i * 4 + 0] = pData[i * 4 + 2];	// R was in slot 2
		pOut[i * 4 + 1] = pData[i * 4 + 1];	// G
		pOut[i * 4 + 2] = pData[i * 4 + 0];	// B was in slot 0
		pOut[i * 4 + 3] = bForceOpaque ? 255 : pData[i * 4 + 3];
	}

	return pOut;
}

//////////////////////////////////////////////////////////////////////////

static unsigned int AllocId()
{
	for (size_t i = 1; i < g_textures.size(); ++i)
	{
		if (!g_textures[i].bUsed)
			return (unsigned int)i;
	}

	if (g_textures.empty())
		g_textures.resize(1);	// burn index 0

	SGLESTexture t;
	memset(&t, 0, sizeof(t));
	g_textures.push_back(t);
	return (unsigned int)(g_textures.size() - 1);
}

static SGLESTexture* Find(unsigned int nId)
{
	if (nId == 0 || nId >= g_textures.size() || !g_textures[nId].bUsed)
		return 0;
	return &g_textures[nId];
}

//////////////////////////////////////////////////////////////////////////

unsigned int GLESTexture_Upload(unsigned int nId, const unsigned char* pData,
                                int nWidth, int nHeight, int eTF,
                                bool bRepeat, bool bMipmap)
{
	if (nWidth <= 0 || nHeight <= 0)
		return 0;

	if (nId == 0)
		nId = AllocId();
	else if (nId >= g_textures.size())
		return 0;

	SGLESTexture& t = g_textures[nId];

	if (!t.nName)
		glGenTextures(1, &t.nName);
	if (!t.nName)
		return 0;

	t.nWidth  = nWidth;
	t.nHeight = nHeight;
	t.eTF     = eTF;
	t.bUsed   = true;

	glBindTexture(GL_TEXTURE_2D, t.nName);

	// Rows are tightly packed. The default is 4-byte alignment, which silently
	// skews any upload whose row length is not a multiple of four -- the
	// classic cause of a texture that looks sheared.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	bool bUploaded = false;

	if (IsCompressed(eTF))
	{
		const GLenum eFormat = CompressedFormat(eTF);
		const int nSize = CompressedSize(eTF, nWidth, nHeight);

		glCompressedTexImage2D(GL_TEXTURE_2D, 0, eFormat, nWidth, nHeight, 0,
		                       nSize, pData);
		bUploaded = true;

		// Mipmaps cannot be generated for a compressed texture -- the driver
		// would have to decompress, filter and recompress. Real DXT assets
		// carry their own mip chain, which this path does not read yet.
		bMipmap = false;
	}
	else if (pData)
	{
		const unsigned char* pUpload = pData;
		unsigned char* pTemp = 0;

		if (eTF != eTF_RGBA)
		{
			// eTF_8888 and eTF_0888 are both BGRA-ordered; 0888 has no
			// meaningful alpha, so it is forced opaque rather than left as
			// whatever happened to be in the byte.
			pTemp = SwizzleBGRAtoRGBA(pData, nWidth * nHeight, eTF == eTF_0888);
			if (!pTemp)
			{
				iLog->LogError("XRenderGLES: out of memory converting a %dx%d texture",
				               nWidth, nHeight);
				return 0;
			}
			pUpload = pTemp;
		}

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, pUpload);
		bUploaded = true;

		if (pTemp)
			free(pTemp);
	}
	else
	{
		// No data: allocate storage only. Render targets and font atlases are
		// created this way and filled later.
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, 0);
		bUploaded = true;
		bMipmap = false;
	}

	const GLint eWrap = bRepeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, eWrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, eWrap);

	if (bMipmap && bUploaded)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	}
	else
	{
		// Without a mip chain the minification filter MUST NOT ask for one, or
		// the texture is incomplete and samples as black. This is the single
		// most common way to get an invisible texture with no error reported.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
	return nId;
}

//////////////////////////////////////////////////////////////////////////

void GLESTexture_UpdateRegion(unsigned int nId, const unsigned char* pData,
                              int nX, int nY, int nWidth, int nHeight, int eTF)
{
	SGLESTexture* pTex = Find(nId);
	if (!pTex || !pData || nWidth <= 0 || nHeight <= 0)
		return;

	if (IsCompressed(pTex->eTF))
	{
		// Partial updates of a compressed texture would have to respect 4x4
		// block boundaries. Nothing asks for it; say so rather than corrupt it.
		iLog->LogWarning("XRenderGLES: sub-image update of a compressed texture "
		                 "is not supported");
		return;
	}

	glBindTexture(GL_TEXTURE_2D, pTex->nName);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	const unsigned char* pUpload = pData;
	unsigned char* pTemp = 0;

	if (eTF != eTF_RGBA)
	{
		pTemp = SwizzleBGRAtoRGBA(pData, nWidth * nHeight, eTF == eTF_0888);
		if (!pTemp)
			return;
		pUpload = pTemp;
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, nX, nY, nWidth, nHeight,
	                GL_RGBA, GL_UNSIGNED_BYTE, pUpload);

	if (pTemp)
		free(pTemp);

	glBindTexture(GL_TEXTURE_2D, 0);
}

//////////////////////////////////////////////////////////////////////////

void GLESTexture_Bind(unsigned int nId)
{
	SGLESTexture* pTex = Find(nId);

	glActiveTexture(GL_TEXTURE0);

	if (!pTex)
	{
		glBindTexture(GL_TEXTURE_2D, 0);
		g_nBound = 0;
		return;
	}

	glBindTexture(GL_TEXTURE_2D, pTex->nName);
	g_nBound = nId;
}

bool GLESTexture_IsBound()
{
	return g_nBound != 0;
}

void GLESTexture_Remove(unsigned int nId)
{
	SGLESTexture* pTex = Find(nId);
	if (!pTex)
		return;

	if (g_nBound == nId)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		g_nBound = 0;
	}

	glDeleteTextures(1, &pTex->nName);

	pTex->nName = 0;
	pTex->bUsed = false;
}

void GLESTexture_Shutdown()
{
	for (size_t i = 1; i < g_textures.size(); ++i)
	{
		if (g_textures[i].nName)
			glDeleteTextures(1, &g_textures[i].nName);
	}

	g_textures.clear();
	g_nBound = 0;
}

#endif //__EMSCRIPTEN__
