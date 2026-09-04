////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESDraw.cpp
//  Description: The dynamic drawing path -- the first geometry this backend
//               puts on screen.
//
//  The engine reaches here through two shapes of the same call:
//
//    DrawDynVB(verts, inds, nVerts, nInds, primType)
//        caller owns the data. CryGame's script renderer uses this.
//
//    GetDynVBPtr(nVerts, nOffs, pool) then DrawDynVB(nOffs, pool, nVerts)
//        the renderer hands out a buffer to fill and draws it afterwards.
//        CryFont builds text this way, six vertices per glyph.
//
//  Both end in DrawDynamic().
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESRenderer.h"
#include "GLESContext.h"
#include "GLESShader.h"

#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#endif

//! How many vertices the pooled path can stage before it has to grow. Sized
//! for a screen of text: CryFont emits six vertices per character.
static const int kDynPoolVerts = 16384;

//////////////////////////////////////////////////////////////////////////
//! Orthographic projection, column-major, matching what the original backend
//! asked GL for: glOrtho(0, w, h, 0, ...).
//!
//! Note the vertical flip. Screen space here has y increasing DOWNWARD -- the
//! engine positions 2D elements from the top-left, as Windows does -- while
//! GL's clip space has y up. Passing top=0 and bottom=h is what inverts it,
//! and it is the reason text and UI are not upside down.
//////////////////////////////////////////////////////////////////////////
static void MakeOrtho(float* m, float fLeft, float fRight, float fBottom, float fTop,
                      float fNear, float fFar)
{
	memset(m, 0, sizeof(float) * 16);

	m[0]  =  2.0f / (fRight - fLeft);
	m[5]  =  2.0f / (fTop - fBottom);
	m[10] = -2.0f / (fFar - fNear);
	m[12] = -(fRight + fLeft) / (fRight - fLeft);
	m[13] = -(fTop + fBottom) / (fTop - fBottom);
	m[14] = -(fFar + fNear) / (fFar - fNear);
	m[15] =  1.0f;
}

static void MakeIdentity(float* m)
{
	memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::Set2DMode(bool enable, int ortox, int ortoy)
{
	m_b2DMode = enable;

	if (enable)
	{
		// The original passed a near/far of -1e30/1e30, which is meaningless
		// for a fixed-point depth buffer and only worked because nothing drawn
		// in 2D was depth tested. A unit range is used here and 2D drawing
		// disables the depth test explicitly.
		MakeOrtho(m_matMVP, 0.0f, (float)ortox, (float)ortoy, 0.0f, -1.0f, 1.0f);
	}
	else
	{
		// 3D needs the camera's view and projection, which this backend does
		// not build yet. Identity keeps the matrix well-formed rather than
		// stale; nothing draws in 3D so far.
		MakeIdentity(m_matMVP);
	}
}

//////////////////////////////////////////////////////////////////////////

bool CGLESRenderer::EnsureDynamicBuffers()
{
#if defined(__EMSCRIPTEN__)
	if (m_nDynVAO)
		return true;

	if (!GLESContext_IsCreated())
		return false;

	glGenVertexArrays(1, &m_nDynVAO);
	glGenBuffers(1, &m_nDynVBO);
	glGenBuffers(1, &m_nDynIBO);

	if (!m_nDynVAO || !m_nDynVBO || !m_nDynIBO)
	{
		iLog->LogError("XRenderGLES: could not create the dynamic vertex buffers");
		return false;
	}

	// The attribute layout never changes for this format, so it is recorded in
	// the VAO once. That is the whole point of a VAO: without one, GLES 3.0
	// would need all three glVertexAttribPointer calls on every draw, each a
	// separate crossing into JavaScript.
	glBindVertexArray(m_nDynVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_nDynVBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_nDynIBO);

	const GLsizei nStride = (GLsizei)sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F);

	glEnableVertexAttribArray(eGLESAttrib_Position);
	glVertexAttribPointer(eGLESAttrib_Position, 3, GL_FLOAT, GL_FALSE, nStride,
	                      (const void*)0);

	// Normalised, so the four bytes arrive in the shader as 0..1. They are in
	// B,G,R,A order; the shader swizzles. See GLESShader.cpp.
	glEnableVertexAttribArray(eGLESAttrib_Color);
	glVertexAttribPointer(eGLESAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, nStride,
	                      (const void*)(sizeof(float) * 3));

	glEnableVertexAttribArray(eGLESAttrib_TexCoord);
	glVertexAttribPointer(eGLESAttrib_TexCoord, 2, GL_FLOAT, GL_FALSE, nStride,
	                      (const void*)(sizeof(float) * 3 + 4));

	glBindVertexArray(0);
	return true;
#else
	return false;
#endif
}

void CGLESRenderer::ReleaseDrawResources()
{
#if defined(__EMSCRIPTEN__)
	if (m_nDynVAO)	glDeleteVertexArrays(1, &m_nDynVAO);
	if (m_nDynVBO)	glDeleteBuffers(1, &m_nDynVBO);
	if (m_nDynIBO)	glDeleteBuffers(1, &m_nDynIBO);

	GLESShader_Shutdown();
#endif

	m_nDynVAO = m_nDynVBO = m_nDynIBO = 0;
	m_nDynVBOCapacity = m_nDynIBOCapacity = 0;

	if (m_pDynPool)
	{
		free(m_pDynPool);
		m_pDynPool = 0;
	}
	m_nDynPoolVerts = m_nDynPoolUsed = 0;
}

//////////////////////////////////////////////////////////////////////////
//! The pooled path: hand out a span for the caller to fill.
//!
//! nOffs is returned in VERTICES, not bytes, and DrawDynVB(nOffs, ...) gets it
//! back. The pool is CPU memory that is uploaded at draw time rather than a
//! mapped GL buffer, because WebGL has no glMapBufferRange -- there is no
//! pointer into GPU memory to hand out from JavaScript's side of the fence.
//////////////////////////////////////////////////////////////////////////
void* CGLESRenderer::GetDynVBPtr(int nVerts, int& nOffs, int Pool)
{
	if (nVerts <= 0)
	{
		nOffs = 0;
		return 0;
	}

	if (!m_pDynPool || m_nDynPoolVerts < nVerts)
	{
		int nWant = nVerts > kDynPoolVerts ? nVerts : kDynPoolVerts;
		void* pNew = realloc(m_pDynPool,
		                     (size_t)nWant * sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F));
		if (!pNew)
		{
			iLog->LogError("XRenderGLES: out of memory growing the dynamic pool "
			               "to %d vertices", nWant);
			nOffs = 0;
			return 0;
		}
		m_pDynPool = pNew;
		m_nDynPoolVerts = nWant;
		m_nDynPoolUsed = 0;
	}

	// Wrap rather than grow without bound. The engine fills and draws within
	// a frame, so a wrap can only overwrite something already submitted.
	if (m_nDynPoolUsed + nVerts > m_nDynPoolVerts)
		m_nDynPoolUsed = 0;

	nOffs = m_nDynPoolUsed;
	m_nDynPoolUsed += nVerts;

	return (struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F*)m_pDynPool + nOffs;
}

void CGLESRenderer::DrawDynVB(int nOffs, int Pool, int nVerts)
{
	if (!m_pDynPool || nVerts <= 0)
		return;

	const struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F* pVerts =
		(const struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F*)m_pDynPool + nOffs;

	// The pooled path is unindexed and always triangles: CryFont writes six
	// vertices per glyph rather than four plus indices.
	DrawDynamic(pVerts, nVerts, 0, 0, R_PRIMV_TRIANGLES);
}

void CGLESRenderer::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F* pBuf, ushort* pInds,
                              int nVerts, int nInds, int nPrimType)
{
	if (!pBuf || nVerts <= 0)
		return;

	DrawDynamic(pBuf, nVerts, pInds, nInds, nPrimType);
}

//////////////////////////////////////////////////////////////////////////
//! The single place that issues geometry.
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::DrawDynamic(const struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F* pVerts, int nVerts,
                                const unsigned short* pInds, int nInds, int nPrimType)
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated() || !EnsureDynamicBuffers())
		return;

	const SGLESProgram* pProgram = GLESShader_GetDynamic();
	if (!pProgram)
		return;

	//////////////////////////////////////////////////////////////////////
	// Primitive translation.
	//
	// GLES has no GL_QUADS, and the engine uses R_PRIMV_QUADS freely -- 2D
	// images and sprites are quads. Each quad becomes two triangles here.
	// Doing it at draw time rather than asking callers to change is what keeps
	// this a backend concern.
	//////////////////////////////////////////////////////////////////////
	GLenum eMode = GL_TRIANGLES;
	const unsigned short* pDrawInds = pInds;
	int nDrawInds = nInds;

	static TArray<unsigned short> s_expanded;

	switch (nPrimType)
	{
	case R_PRIMV_TRIANGLES:
		eMode = GL_TRIANGLES;
		break;

	case R_PRIMV_TRIANGLE_STRIP:
		eMode = GL_TRIANGLE_STRIP;
		break;

	case R_PRIMV_TRIANGLE_FAN:
		eMode = GL_TRIANGLE_FAN;
		break;

	case R_PRIMV_QUADS:
	{
		eMode = GL_TRIANGLES;

		// Quads arrive either as indices in groups of four, or -- when
		// unindexed -- as vertices in groups of four.
		const int nQuads = (nInds > 0 ? nInds : nVerts) / 4;
		s_expanded.SetUse(0);
		s_expanded.Reserve(nQuads * 6);

		for (int q = 0; q < nQuads; ++q)
		{
			unsigned short a, b, c, d;
			if (nInds > 0)
			{
				a = pInds[q * 4 + 0]; b = pInds[q * 4 + 1];
				c = pInds[q * 4 + 2]; d = pInds[q * 4 + 3];
			}
			else
			{
				a = (unsigned short)(q * 4 + 0); b = (unsigned short)(q * 4 + 1);
				c = (unsigned short)(q * 4 + 2); d = (unsigned short)(q * 4 + 3);
			}

			s_expanded.AddElem(a); s_expanded.AddElem(b); s_expanded.AddElem(c);
			s_expanded.AddElem(a); s_expanded.AddElem(c); s_expanded.AddElem(d);
		}

		pDrawInds = s_expanded.Num() ? &s_expanded[0] : 0;
		nDrawInds = s_expanded.Num();
		break;
	}

	default:
		// R_PRIMV_MULTI_STRIPS and MULTI_GROUPS carry their own sub-range
		// tables and are not reachable from DrawDynVB. Saying so once beats
		// drawing something wrong.
		{
			static bool s_bWarned = false;
			if (!s_bWarned)
			{
				s_bWarned = true;
				iLog->LogWarning("XRenderGLES: DrawDynVB primitive type %d is not "
				                 "implemented; skipped", nPrimType);
			}
			return;
		}
	}

	//////////////////////////////////////////////////////////////////////
	// Upload.
	//
	// glBufferData with a null pointer first is buffer orphaning: it tells the
	// driver the old contents are dead, so it can hand back fresh storage
	// instead of waiting for in-flight draws to finish reading the old. Without
	// it, a buffer written every frame serialises the CPU against the GPU.
	//////////////////////////////////////////////////////////////////////
	glBindVertexArray(m_nDynVAO);

	const int nVBBytes = nVerts * (int)sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F);
	glBindBuffer(GL_ARRAY_BUFFER, m_nDynVBO);
	if (nVBBytes > m_nDynVBOCapacity)
	{
		glBufferData(GL_ARRAY_BUFFER, nVBBytes, 0, GL_STREAM_DRAW);
		m_nDynVBOCapacity = nVBBytes;
	}
	else
	{
		glBufferData(GL_ARRAY_BUFFER, m_nDynVBOCapacity, 0, GL_STREAM_DRAW);
	}
	glBufferSubData(GL_ARRAY_BUFFER, 0, nVBBytes, pVerts);

	if (nDrawInds > 0 && pDrawInds)
	{
		const int nIBBytes = nDrawInds * (int)sizeof(unsigned short);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_nDynIBO);
		if (nIBBytes > m_nDynIBOCapacity)
		{
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, nIBBytes, 0, GL_STREAM_DRAW);
			m_nDynIBOCapacity = nIBBytes;
		}
		else
		{
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_nDynIBOCapacity, 0, GL_STREAM_DRAW);
		}
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, nIBBytes, pDrawInds);
	}

	//////////////////////////////////////////////////////////////////////
	// State and draw.
	//////////////////////////////////////////////////////////////////////
	glUseProgram(pProgram->nProgram);

	if (pProgram->nMVP >= 0)
		glUniformMatrix4fv(pProgram->nMVP, 1, GL_FALSE, m_matMVP);

	// Untextured until the texture manager is written against GLES. Vertex
	// colour alone is what the engine's debug drawing and untextured UI use,
	// and it is honest about what is and is not implemented.
	if (pProgram->nUseTexture >= 0)
		glUniform1i(pProgram->nUseTexture, 0);

	if (m_b2DMode)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
	}

	if (nDrawInds > 0 && pDrawInds)
		glDrawElements(eMode, nDrawInds, GL_UNSIGNED_SHORT, (const void*)0);
	else
		glDrawArrays(eMode, 0, nVerts);

	glBindVertexArray(0);
#endif
}
