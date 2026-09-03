################################################################################
# CryPhysics -- rigid-body, living-entity, rope and soft-body simulation.
#
# validator.h is reconstructed here -- another header Crytek's Linux build
# had that was never released. See its comment block; the definitions come
# verbatim from the #else branch beside the include in utils.h.
#
# Compiles, does not link: like the other engine modules it calls into code that
# is not ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryPhysics
    SOURCES
    CryPhysics.cpp
    StdAfx.cpp
    aabbtree.cpp
    articulatedentity.cpp
    boolean2d.cpp
    boxgeom.cpp
    cylindergeom.cpp
    geoman.cpp
    geometry.cpp
    heightfieldbv.cpp
    heightfieldgeom.cpp
    intersectionchecks.cpp
    linunprojectionchecks.cpp
    livingentity.cpp
    matrixnm.cpp
    obbtree.cpp
    overlapchecks.cpp
    particleentity.cpp
    physicalentity.cpp
    physicalplaceholder.cpp
    physicalworld.cpp
    qhull.cpp
    raybv.cpp
    raygeom.cpp
    rigidbody.cpp
    rigidentity.cpp
    ropeentity.cpp
    rotunprojectionchecks.cpp
    singleboxtree.cpp
    softentity.cpp
    spheregeom.cpp
    trimesh.cpp
    utils.cpp
    wheeledvehicleentity.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"

    DEPENDS
        CryCommon
)
