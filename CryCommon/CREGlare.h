// [webport] This header was not self-contained: it names types it never
// included, relying on the .vcproj's fixed compile order to have pulled
// them in first. Including its real dependencies lets it stand alone.
#include "IRenderer.h"


#ifndef __CREGLARE_H__
#define __CREGLARE_H__

//=============================================================

struct SByteColor
{
  byte r,g,b,a;
};

struct SLongColor
{
  unsigned int r,g,b,a;
};


class CREGlare : public CRendElement
{
public:
  int m_GlareWidth;
  int m_GlareHeight;
  float m_fGlareAmount;

public:
  CREGlare()
  {
    mfInit();
  }
  void mfInit();

  virtual ~CREGlare()
  {
  }

  virtual void mfPrepare();
  virtual bool mfDraw(SShader *ef, SShaderPass *sfm);
};

#endif  // __CREGLARE_H__
