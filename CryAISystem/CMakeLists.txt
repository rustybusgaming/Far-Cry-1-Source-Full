################################################################################
# CryAISystem -- AI pathfinding helpers.
#
# NOTE: this module is INCOMPLETE in the released source. CAISystem, AIObject,
# AIPlayer and GoalOp are absent entirely -- headers and sources -- so only the
# graph/vertex-list helpers can be built. The five translation units that need
# the missing classes are listed in tools/triage.py --excluded. Nothing can be
# done about that here; the code is simply not in the drop.
#
# Compiles, does not link: like Cry3DEngine it calls into modules that are not
# ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryAISystem
    SOURCES
    IAgent.cpp
    StdAfx.cpp
    VertexList.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"

    DEPENDS
        CryCommon
)
