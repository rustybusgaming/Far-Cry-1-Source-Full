################################################################################
# CrySoundSystem -- the sound and music front end.
#
# NOTE: this module WRAPS crysound.lib, which is FMOD 3.61 rebranded and
# ships as a Windows binary with no source. It compiles, but it cannot link
# or run until that engine is replaced -- OpenAL or WebAudio being the
# obvious targets. Compiling is still worth having: it is the wrapper that
# the replacement has to satisfy.
#
# Compiles, does not link: like the other engine modules it calls into code that
# is not ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CrySoundSystem
    SOURCES
    ADPCMDecoder.cpp
    CrySoundSystem.cpp
    MusicPattern.cpp
    MusicPatternInstance.cpp
    MusicSystem.cpp
    OGGDecoder.cpp
    PCMDecoder.cpp
    RandGen.cpp
    Sound.cpp
    SoundBuffer.cpp
    SoundSystem.cpp
    SoundSystemCommon.cpp
    StdAfx.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/OggVorbisInclude"
        "${CMAKE_CURRENT_SOURCE_DIR}/OggVorbisInclude/vorbis"
        "${CMAKE_CURRENT_SOURCE_DIR}/OggVorbisInclude/ogg"
    DEPENDS
        CryCommon
)
