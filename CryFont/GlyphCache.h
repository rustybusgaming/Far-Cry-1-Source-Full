//-------------------------------------------------------------------------------------------------
// Author: Márcio Martins
//
// Purpose:
//  - Manage and cache glyphs, retrieving them from the renderer as needed
//
// History:
//  - [6/6/2003] created the file
//
//-------------------------------------------------------------------------------------------------
#pragma once



// [webport] LINUX now takes the same route as WIN64: <map> with hash_map
// aliased to map.
//
// The old LINUX branch pulled <ext/hash_map>, which puts hash_map in
// __gnu_cxx, not std -- so the std::hash_map typedefs below never resolved.
// Fixing the namespace would not have been enough either: <ext/hash_map> is a
// deprecated libstdc++ extension, and Emscripten builds against libc++, where
// it does not exist at all. It would have broken again on the real target.
//
// std::map is ordered rather than hashed, so lookups are O(log n) instead of
// O(1). For a glyph cache holding a few hundred entries that is immaterial,
// and it is exactly the trade the WIN64 branch already accepted.
#if defined(WIN64) || defined(LINUX)
#include <map>
#define hash_map map
#else
#include <hash_map>
#endif

#include <vector>
#include "GlyphBitmap.h"
#include "FontRenderer.h"



typedef struct CCacheSlot
{
	unsigned int	dwUsage;
	int				iCacheSlot;
	wchar_t			cCurrentChar;

	int				iCharWidth;
	int				iCharHeight;
    
	CGlyphBitmap	pGlyphBitmap;

	void			Reset()
	{
		dwUsage = 0;
		cCurrentChar = -1;

		iCharWidth = 0;
		iCharHeight = 0;

		pGlyphBitmap.Clear();
	}

} CCacheSlot;


typedef std::hash_map<wchar_t, CCacheSlot *>			CCacheTable;
typedef std::hash_map<wchar_t, CCacheSlot *>::iterator	CCacheTableItor;

typedef std::vector<CCacheSlot *>						CCacheSlotList;
typedef std::vector<CCacheSlot *>::iterator				CCacheSlotListItor;


#ifdef WIN64
#undef GetCharWidth
#undef GetCharHeight
#endif

class CGlyphCache
{
public:
	CGlyphCache();
	~CGlyphCache();

	int Create(int iChacheSize, int iGlyphBitmapWidth, int iGlyphBitmapHeight, int iSmoothMethod, int iSmoothAmount, float fSizeRatio = 0.8f);
	int Release();

	int LoadFontFromFile(const string &szFileName);
	int LoadFontFromMemory(unsigned char *pFileBuffer, int iDataSize);
	int ReleaseFont();

	int SetEncoding(FT_Encoding pEncoding) { return m_pFontRenderer.SetEncoding(pEncoding); };
	FT_Encoding GetEncoding() { return m_pFontRenderer.GetEncoding(); };

	int	GetGlyphBitmapSize(int *pWidth, int *pHeight);

	int PreCacheGlyph(wchar_t cChar);
	int UnCacheGlyph(wchar_t cChar);
	int GlyphCached(wchar_t cChar);

	CCacheSlot *GetLRUSlot();
	CCacheSlot *GetMRUSlot();

	int GetGlyph(CGlyphBitmap **pGlyph, int *piWidth, int *piHeight, wchar_t cChar);

private:

	int				CreateSlotList(int iListSize);
	int				ReleaseSlotList();

	CCacheSlotList	m_pSlotList;
	CCacheTable		m_pCacheTable;

	int				m_iGlyphBitmapWidth;
	int				m_iGlyphBitmapHeight;
	float			m_fSizeRatio;

	int				m_iSmoothMethod;
	int				m_iSmoothAmount;

	CGlyphBitmap	*m_pScaleBitmap;

	CFontRenderer	m_pFontRenderer;

	unsigned int	m_dwUsage;
};