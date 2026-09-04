################################################################################
# Web -- the browser host.
#
# Same module set as Headless with one substitution: CryRenderGLES instead of
# CryRenderNULL. The two backends both define PackageRenderConstructor and the
# renderer's module globals, so exactly one can be in a link -- which is why
# this is a separate target rather than a switch on the headless one.
#
# wasm only. The GLES backend needs a WebGL context, and there is no GL of any
# kind on the native target (see RenderDll/CMakeLists.txt).
################################################################################

if(NOT EMSCRIPTEN)
    message(STATUS "cry: Web host skipped (needs Emscripten)")
    return()
endif()

add_executable(CryWeb WebMain.cpp)
cry_apply_common_settings(CryWeb)

target_include_directories(CryWeb PRIVATE
    ${CMAKE_SOURCE_DIR}/CryCommon
    ${CMAKE_SOURCE_DIR}/Hosts
)

# CRY_HEADLESS_MODULES with the renderer swapped. Built by string replacement
# rather than a second hand-maintained list, so a module added to one host
# cannot be forgotten in the other.
set(_web_modules "${CRY_HEADLESS_MODULES}")
list(REMOVE_ITEM _web_modules CryRenderNULL)
list(APPEND _web_modules CryRenderGLES)

cry_link_module_group(CryWeb ${_web_modules})
target_link_libraries(CryWeb PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)

# A canvas is required, so this is an HTML target. Emscripten's default shell
# provides the #canvas element the renderer looks for.
set_target_properties(CryWeb PROPERTIES SUFFIX ".html")

message(STATUS "cry: executable CryWeb (browser host)")
