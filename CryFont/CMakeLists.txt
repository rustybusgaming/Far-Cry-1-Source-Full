################################################################################
# CryFont -- font rendering, with FreeType2 vendored under FreeType2/.
#
# FreeType itself is built separately as CryFreeType (see cmake/CryVendor.cmake),
# because it is C rather than C++ and follows its own one-TU-per-module build
# convention.
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
        CryFreeType
)
