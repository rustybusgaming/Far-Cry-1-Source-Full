////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESVertexBuffer.cpp
//  Description: Static vertex and index buffers -- the path world geometry
//               takes.
//
//  THE FORMAT PROBLEM, AND WHY THIS IS NOT A SWITCH STATEMENT
//
//  The engine has seventeen vertex formats. XRenderOGL handles them with a
//  switch per entry point, each case naming a struct and calling the matching
//  glVertexPointer/glColorPointer/glTexCoordPointer trio -- around 200 lines
//  repeated in several places, and a place to forget a format.
//
//  It does not need to be written that way, because the engine already ships
//  the description: CryCommon/VertexFormats.h has m_VertexSize[] giving the
//  stride of every format and gBufInfoTable[] giving the byte offset of the
//  colour, texture coordinate, secondary colour and normal within it. Those two
//  tables are enough to bind any format generically, so this file has one
//  binder rather than seventeen cases, and a format added to the engine works
//  here without a change.
//
//  The tables use 0 to mean "this format has no such attribute" -- position is
//  always at offset 0, so a zero colour offset cannot be a real one. That is
//  the same convention CLeafBuffer reads them with.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESRenderer.h"
#include "GLESContext.h"
#include "GLESShader.h"
#include "GLESTexture.h"

#include <map>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#endif

#if defined(__EMSCRIPTEN__)

//! A VAO per GL vertex buffer. The attribute layout of a static buffer never
//! changes, so it is worth recording once -- every glVertexAttribPointer is a
//! separate crossing into JavaScript, and a mesh drawn every frame would
//! otherwise pay for three of them each time.
static std::map<GLuint, GLuint>	g_vaos;

//////////////////////////////////////////////////////////////////////////
//! Describe a vertex format to GL from the engine's own tables.
//////////////////////////////////////////////////////////////////////////
static void BindVertexFormat(int nFormat)
{
	if (nFormat <= 0 || nFormat >= VERTEX_FORMAT_NUMS)
		return;

	const GLsizei nStride = (GLsizei)m_VertexSize[nFormat];
	const SBufInfoTable* pOffs = &gBufInfoTable[nFormat];

	// Position is at offset 0 in every format.
	//
	// TRP3F is the exception worth knowing about: its position is four floats,
	// x/y/z/rhw, already transformed into screen space. Binding three of them
	// reads x/y/z correctly and ignores rhw, which is right as long as the
	// draw is in 2D mode -- which is the only place the engine uses this
	// format. DrawBuffer says so if that is not the case.
	glEnableVertexAttribArray(eGLESAttrib_Position);
	glVertexAttribPointer(eGLESAttrib_Position, 3, GL_FLOAT, GL_FALSE, nStride,
	                      (const void*)0);

	if (pOffs->OffsColor)
	{
		glEnableVertexAttribArray(eGLESAttrib_Color);
		glVertexAttribPointer(eGLESAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, nStride,
		                      (const void*)(intptr_t)pOffs->OffsColor);
	}
	else
	{
		// A disabled attribute keeps whatever constant was last set for it, so
		// it has to be set rather than assumed. White leaves the texture or the
		// material colour to decide.
		glDisableVertexAttribArray(eGLESAttrib_Color);
		glVertexAttrib4f(eGLESAttrib_Color, 1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (pOffs->OffsTC)
	{
		glEnableVertexAttribArray(eGLESAttrib_TexCoord);
		glVertexAttribPointer(eGLESAttrib_TexCoord, 2, GL_FLOAT, GL_FALSE, nStride,
		                      (const void*)(intptr_t)pOffs->OffsTC);
	}
	else
	{
		glDisableVertexAttribArray(eGLESAttrib_TexCoord);
		glVertexAttrib2f(eGLESAttrib_TexCoord, 0.0f, 0.0f);
	}
}

//! The VAO for a buffer, created and configured on first use.
static GLuint GetVAO(GLuint nVBO, GLuint nIBO, int nFormat)
{
	std::map<GLuint, GLuint>::iterator it = g_vaos.find(nVBO);
	if (it != g_vaos.end())
	{
		glBindVertexArray(it->second);
		if (nIBO)
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nIBO);
		return it->second;
	}

	GLuint nVAO = 0;
	glGenVertexArrays(1, &nVAO);
	if (!nVAO)
		return 0;

	glBindVertexArray(nVAO);
	glBindBuffer(GL_ARRAY_BUFFER, nVBO);
	if (nIBO)
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nIBO);

	BindVertexFormat(nFormat);

	g_vaos[nVBO] = nVAO;
	return nVAO;
}

static void ForgetVAO(GLuint nVBO)
{
	std::map<GLuint, GLuint>::iterator it = g_vaos.find(nVBO);
	if (it == g_vaos.end())
		return;

	glDeleteVertexArrays(1, &it->second);
	g_vaos.erase(it);
}

#endif //__EMSCRIPTEN__

//////////////////////////////////////////////////////////////////////////
// Creation and lifetime.
//////////////////////////////////////////////////////////////////////////

CVertexBuffer* CGLESRenderer::CreateBuffer(int buffersize, int vertexformat,
                                           const char* szSource, bool bDynamic)
{
	CVertexBuffer* pBuf = new CVertexBuffer;
	pBuf->m_vertexformat = vertexformat;
	pBuf->m_NumVerts = buffersize;

	CreateBuffer(buffersize, vertexformat, pBuf, VSF_GENERAL, szSource);
	return pBuf;
}

void CGLESRenderer::CreateBuffer(int size, int vertexformat, CVertexBuffer* buf,
                                 int Type, const char* szSource)
{
	if (!buf || Type < 0 || Type >= VSF_NUM)
		return;

#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	SVertexStream& vs = buf->m_VS[Type];

	// The engine keeps its own system-memory copy and expects to be able to
	// write to it between draws, so the CPU allocation is real rather than a
	// staging convenience. UpdateBuffer is what pushes it to GL.
	const int nStride = (vertexformat > 0 && vertexformat < VERTEX_FORMAT_NUMS)
	                  ? m_VertexSize[vertexformat] : 0;
	if (nStride <= 0)
	{
		iLog->LogWarning("XRenderGLES: CreateBuffer with unknown vertex format %d",
		                 vertexformat);
		return;
	}

	GLuint nVBO = 0;
	glGenBuffers(1, &nVBO);
	if (!nVBO)
		return;

	glBindBuffer(GL_ARRAY_BUFFER, nVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size * nStride, 0,
	             vs.m_bDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	vs.m_VertBuf.m_nID = nVBO;
	vs.m_nItems = size;
#endif
}

void CGLESRenderer::ReleaseBuffer(CVertexBuffer* bufptr)
{
	if (!bufptr)
		return;

#if defined(__EMSCRIPTEN__)
	for (int i = 0; i < VSF_NUM; ++i)
	{
		GLuint nVBO = (GLuint)bufptr->m_VS[i].m_VertBuf.m_nID;
		if (!nVBO)
			continue;

		ForgetVAO(nVBO);
		glDeleteBuffers(1, &nVBO);
		bufptr->m_VS[i].m_VertBuf.m_nID = 0;
	}
#endif

	delete bufptr;
}

void CGLESRenderer::UpdateBuffer(CVertexBuffer* dest, const void* src, int size,
                                 bool bUnlock, int offs, int Type)
{
	if (!dest || !src || Type < 0 || Type >= VSF_NUM)
		return;

#if defined(__EMSCRIPTEN__)
	const GLuint nVBO = (GLuint)dest->m_VS[Type].m_VertBuf.m_nID;
	if (!nVBO || !GLESContext_IsCreated())
		return;

	const int nFormat = dest->m_vertexformat;
	const int nStride = (nFormat > 0 && nFormat < VERTEX_FORMAT_NUMS)
	                  ? m_VertexSize[nFormat] : 0;
	if (nStride <= 0)
		return;

	glBindBuffer(GL_ARRAY_BUFFER, nVBO);
	glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offs * nStride,
	                (GLsizeiptr)size * nStride, src);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Index buffers.
//
// The engine's indices are 16-bit throughout, which caps a single buffer at
// 65536 vertices. That is a real limit and it belongs to the data, not to this
// backend -- WebGL2 does support 32-bit indices, so it is the meshes that would
// have to change, not the renderer.
//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::CreateIndexBuffer(SVertexStream* dest, const void* src, int indexcount)
{
	if (!dest)
		return;

#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	GLuint nIBO = (GLuint)dest->m_VertBuf.m_nID;
	if (!nIBO)
	{
		glGenBuffers(1, &nIBO);
		if (!nIBO)
			return;
		dest->m_VertBuf.m_nID = nIBO;
	}

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             (GLsizeiptr)indexcount * sizeof(unsigned short), src,
	             dest->m_bDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	dest->m_nItems = indexcount;
#endif
}

void CGLESRenderer::UpdateIndexBuffer(SVertexStream* dest, const void* src,
                                      int indexcount, bool bUnLock)
{
	if (!dest || !src)
		return;

#if defined(__EMSCRIPTEN__)
	const GLuint nIBO = (GLuint)dest->m_VertBuf.m_nID;
	if (!nIBO || !GLESContext_IsCreated())
		return;

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, nIBO);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
	                (GLsizeiptr)indexcount * sizeof(unsigned short), src);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif
}

void CGLESRenderer::ReleaseIndexBuffer(SVertexStream* dest)
{
	if (!dest)
		return;

#if defined(__EMSCRIPTEN__)
	GLuint nIBO = (GLuint)dest->m_VertBuf.m_nID;
	if (nIBO)
		glDeleteBuffers(1, &nIBO);
#endif

	dest->m_VertBuf.m_nID = 0;
	dest->m_nItems = 0;
}

//////////////////////////////////////////////////////////////////////////
// Drawing.
//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::DrawBuffer(CVertexBuffer* src, SVertexStream* indicies,
                               int numindices, int offsindex, int prmode,
                               int vert_start, int vert_stop, CMatInfo* mi)
{
#if defined(__EMSCRIPTEN__)
	if (!src || !GLESContext_IsCreated())
		return;

	const GLuint nVBO = (GLuint)src->m_VS[VSF_GENERAL].m_VertBuf.m_nID;
	if (!nVBO)
		return;

	const SGLESProgram* pProgram = GLESShader_GetDynamic();
	if (!pProgram)
		return;

	const int nFormat = src->m_vertexformat;

	if (nFormat == VERTEX_FORMAT_TRP3F_COL4UB_TEX2F && !m_b2DMode)
	{
		// Pre-transformed vertices are already in screen space; running them
		// through a projection would move them somewhere arbitrary. The engine
		// only uses this format for 2D, so this should not happen -- but it
		// would be invisible if it did.
		static bool s_bWarned = false;
		if (!s_bWarned)
		{
			s_bWarned = true;
			iLog->LogWarning("XRenderGLES: pre-transformed vertices drawn outside "
			                 "2D mode; positions will be wrong");
		}
	}

	const GLuint nIBO = indicies ? (GLuint)indicies->m_VertBuf.m_nID : 0;

	if (!GetVAO(nVBO, nIBO, nFormat))
		return;

	GLenum eMode;
	switch (prmode)
	{
	case R_PRIMV_TRIANGLES:			eMode = GL_TRIANGLES; break;
	case R_PRIMV_TRIANGLE_STRIP:	eMode = GL_TRIANGLE_STRIP; break;
	case R_PRIMV_TRIANGLE_FAN:		eMode = GL_TRIANGLE_FAN; break;
	default:
		{
			// R_PRIMV_QUADS is expanded in the dynamic path because the indices
			// are the caller's to rewrite. Here they are already in a GL buffer
			// and rewriting them would mean reading them back.
			static bool s_bWarned = false;
			if (!s_bWarned)
			{
				s_bWarned = true;
				iLog->LogWarning("XRenderGLES: DrawBuffer primitive type %d is not "
				                 "implemented; skipped", prmode);
			}
			glBindVertexArray(0);
			return;
		}
	}

	glUseProgram(pProgram->nProgram);

	if (pProgram->nMVP >= 0)
		glUniformMatrix4fv(pProgram->nMVP, 1, GL_FALSE, m_matMVP);

	const bool bTextured = GLESTexture_IsBound();
	if (pProgram->nUseTexture >= 0)
		glUniform1i(pProgram->nUseTexture, bTextured ? 1 : 0);
	if (bTextured && pProgram->nSampler >= 0)
		glUniform1i(pProgram->nSampler, 0);

	if (nIBO && numindices > 0)
	{
		// offsindex counts INDICES, not bytes.
		glDrawElements(eMode, numindices, GL_UNSIGNED_SHORT,
		               (const void*)(intptr_t)(offsindex * (int)sizeof(unsigned short)));
	}
	else
	{
		const int nFirst = vert_start;
		const int nCount = (vert_stop > vert_start)
		                 ? (vert_stop - vert_start)
		                 : (src->m_NumVerts - vert_start);
		if (nCount > 0)
			glDrawArrays(eMode, nFirst, nCount);
	}

	glBindVertexArray(0);
#endif
}

void CGLESRenderer::DrawTriStrip(CVertexBuffer* src, int vert_num)
{
	DrawBuffer(src, 0, 0, 0, R_PRIMV_TRIANGLE_STRIP, 0, vert_num, 0);
}
