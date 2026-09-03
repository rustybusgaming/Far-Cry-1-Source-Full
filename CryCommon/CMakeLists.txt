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
    CRE2DQuad.h
    CREDummy.h
    CREFlashBang.h
    CREGlare.h
    CREOcLeaf.h
    CREOcclusionQuery.h
    CREPolyMesh.h
    CREScreenProcess.h
    CREShadowMap.h
    CRESky.h
    CRETerrainSector.h
    CRETriMeshAdditionalShadow.h
    CRETriMeshShadow.h
    ColorDefs.h
    CrtDebugStats.h
    CryAnimationScriptCommands.h
    CryCharAnimationParams.h
    CryCharFxTrailParams.h
    CryCharMorphParams.h
    CryCompiledFile.h
    CryEngineDecalInfo.h
    CryFile.h
    CryHeaders.h
    CryLibrary.h
    CryMemoryManager.h
    CryParticleSpawnInfo.h
    CrySizer.h
    CryVersion.h
    Cry_Camera.h
    Cry_Color4.h
    Cry_Geo.h
    Cry_GeoDistance.h
    Cry_GeoIntersect.h
    Cry_GeoOverlap.h
    Cry_Math.h
    Cry_MathFwd.h
    Cry_Matrix.h
    Cry_Quat.h
    Cry_Vector2.h
    Cry_Vector3.h
    Cry_XOptimise.h
    EntityDesc.h
    Font.h
    FrameProfiler.h
    IAISystem.h
    IAgent.h
    IBindable.h
    IBitStream.h
    ICompressionHelper.h
    IConsole.h
    ICryAnimation.h
    ICryPak.h
    IDataProbe.h
    IEdgeConnectivityBuilder.h
    IEntityRenderState.h
    IEntitySystem.h
    IFont.h
    IGame.h
    IInput.h
    ILMSerializationManager.h
    ILog.h
    IMarkers.h
    IMiniLog.h
    IMovieSystem.h
    INetwork.h
    IPAddress.h
    IPhysics.h
    IProcess.h
    IRenderer.h
    IScriptSystem.h
    IShader.h
    ISound.h
    IStatObj.h
    IStreamEngine.h
    IStreamPersist.h
    ISystem.h
    ITimer.h
    IValidator.h
    IXGame.h
    IXMLDOM.h
    IXml.h
    LMCompStructures.h
    LeafBuffer.h
    Linux64Specific.h
    LinuxSpecific.h
    MakMatInfoFromMAT_ENTITY.h
    MeshIdx.h
    Names.h
    ProjectDefines.h
    Range.h
    ScriptObjectVector.h
    SerializeBuffer.h
    Splash.h
    StlUtils.h
    Stream.h
    StringUtils.h
    TAlloc.h
    TArrays.h
    TString.h
    Tarray.h
    TimeValue.h
    VertexBufferSource.h
    VertexFormats.h
    WinBase.h
    XmlParserFont.h
    XmlStream.h
    _ScriptableEx.h
    crysound.h
    crysound64.h
    list2.h
    matrixnm.h
    platform.h
    primitives.h
    smartptr.h
    )
    cry_add_header_gate(CryCommonHeaderGate
        HEADERS ${CRYCOMMON_GATED_HEADERS}
        INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
