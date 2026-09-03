################################################################################
# CryStaticModules -- the compiled-in module table.
#
# Kept as its own target rather than folded into CrySystem so the dependency
# direction stays honest: CrySystem does not know which modules exist, it only
# asks for them by name. This library is the thing that knows, and it is
# therefore the thing that pulls every module into the link.
################################################################################

cry_add_module(CryStaticModules
    SOURCES
        StaticModules.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/CryCommon
        ${CMAKE_SOURCE_DIR}/CryMovie
)
