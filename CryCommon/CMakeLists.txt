################################################################################
# CryCommon -- the engine's shared interface module.
#
# CryCommon is 130 headers and zero .cpp files, so there is nothing here to
# compile in the ordinary sense. Instead each header is compiled as its own
# translation unit (see cry_add_header_gate), which proves the header is
# self-contained and turns "does the port build" into a per-header score.
#
# The list below is the set that currently passes. It is an explicit allowlist
# rather than a glob, on purpose: the build stays green, so any NEW breakage is
# a real regression -- and moving a header into this list is the unit of
# progress for the port. Run tools/triage.py to see what is still outstanding.
################################################################################

# Interface target the other modules link against, for the include path.
add_library(CryCommon INTERFACE)
target_include_directories(CryCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(CryCommon INTERFACE ${CRY_COMMON_DEFINES})

if(CRY_HEADER_PARSE_GATE)
    set(CRYCOMMON_GATED_HEADERS
    AgentParams.h
    AnimKey.h
    CREShadowMap.h
    CRETriMeshAdditionalShadow.h
    CrtDebugStats.h
    CryAnimationScriptCommands.h
    CryCharAnimationParams.h
    CryCharMorphParams.h
    CryEngineDecalInfo.h
    CryLibrary.h
    CryMemoryManager.h
    CrySizer.h
    CryVersion.h
    Cry_Camera.h
    Cry_Color4.h
    Cry_Geo.h
    Cry_GeoIntersect.h
    Cry_GeoOverlap.h
    Cry_Math.h
    Cry_MathFwd.h
    Cry_Matrix.h
    Cry_Quat.h
    Cry_Vector2.h
    Cry_Vector3.h
    FrameProfiler.h
    IAISystem.h
    IAgent.h
    IBindable.h
    ICompressionHelper.h
    IConsole.h
    IDataProbe.h
    IFont.h
    IGame.h
    IInput.h
    ILog.h
    IMiniLog.h
    INetwork.h
    IProcess.h
    IScriptSystem.h
    IStatObj.h
    IStreamEngine.h
    IStreamPersist.h
    ISystem.h
    ITimer.h
    IValidator.h
    IXGame.h
    IXMLDOM.h
    Linux64Specific.h
    LinuxSpecific.h
    ProjectDefines.h
    Range.h
    StlUtils.h
    TAlloc.h
    TArrays.h
    TString.h
    Tarray.h
    TimeValue.h
    WinBase.h
    XmlParserFont.h
    XmlStream.h
    _ScriptableEx.h
    crysound.h
    crysound64.h
    matrixnm.h
    platform.h
    smartptr.h
    )
    cry_add_header_gate(CryCommonHeaderGate
        HEADERS ${CRYCOMMON_GATED_HEADERS}
        INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
