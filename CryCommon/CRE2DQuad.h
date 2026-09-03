// [webport] This header was not self-contained: it names types it never
// included, relying on the .vcproj's fixed compile order to have pulled
// them in first. Including its real dependencies lets it stand alone.
#include "IRenderer.h"

#ifndef _CRE2DQuad_H_
#define _CRE2DQuad_H_

#include "VertexFormats.h"

class CRE2DQuad: public CRendElement
{
  friend class CRender3D;

public:

  CRE2DQuad()
  {
    mfSetType(eDATA_2DQuad);
    mfUpdateFlags(FCEF_TRANSFORM);
  }

  virtual ~CRE2DQuad()
  {
  }

  virtual void mfPrepare();
  virtual bool mfDraw(SShader *ef, SShaderPass *sfm);

  virtual void *mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags);

  struct_VERTEX_FORMAT_P3F_TEX2F m_arrVerts[4];
};

#endif _CRE2DQuad_H_
