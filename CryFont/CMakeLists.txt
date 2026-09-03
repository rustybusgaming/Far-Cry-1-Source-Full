################################################################################
# CryFont -- font rendering, with FreeType2 vendored under FreeType2/.
#
# Only CryFont's own sources are built here; FreeType itself is a separate
# vendored library and is not yet compiled.
#
# Compiles, does not link: like the other engine modules it calls into code that
# is not ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryFont
    SOURCES
    CryFont.cpp
    CryPakIO.cpp
    FBitmap.cpp
    FFont.cpp
    FFontXML.cpp
    FontRenderer.cpp
    FontTexture.cpp
    GlyphBitmap.cpp
    GlyphCache.cpp
    ICryFont.cpp
    StdAfx.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeType2/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeType2/include/freetype/config"
        "${CMAKE_CURRENT_SOURCE_DIR}/FreeType2"
    DEPENDS
        CryCommon
)
