################################################################################
# Tests for the web port's own code.
#
# Scope is deliberately narrow: this is not a test suite for CryEngine. It
# covers the code THIS PORT introduces -- principally WinBase.h, which is a
# reconstruction of a header that was never released and therefore has no
# original to check against.
################################################################################

add_executable(test_winbase test_winbase.cpp)
cry_apply_common_settings(test_winbase)
target_link_libraries(test_winbase PRIVATE CryCommon)

add_test(NAME winbase COMMAND test_winbase)

# The browser input backend. Its event source is separable precisely so the
# state machine can be tested here -- none of it can run in a browser under a
# unit test.
add_executable(test_webinput test_webinput.cpp
               "${PROJECT_SOURCE_DIR}/CryInput/WebInput.cpp")
cry_apply_common_settings(test_webinput)
target_include_directories(test_webinput PRIVATE "${PROJECT_SOURCE_DIR}/CryInput")
target_link_libraries(test_webinput PRIVATE CryCommon)

add_test(NAME webinput COMMAND test_webinput)

# The compiled-in module registry that replaces dlopen()/dlsym(). It links the
# whole module set, because the point of the test is that the real factories
# resolve -- a table checked against stubs would prove nothing.
add_executable(test_static_modules test_static_modules.cpp)
cry_apply_common_settings(test_static_modules)
target_include_directories(test_static_modules PRIVATE "${PROJECT_SOURCE_DIR}/CryCommon")
cry_link_module_group(test_static_modules ${CRY_HEADLESS_MODULES})
target_link_libraries(test_static_modules PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)

add_test(NAME static_modules COMMAND test_static_modules)

# The headless engine itself: does CreateSystemInterface() bring up every
# subsystem and shut down again. This is a smoke test, not a unit test -- it is
# here because it is the only thing that exercises the modules TOGETHER, and
# because the failure it guards against (a subsystem quietly dropping out of
# startup) is invisible to everything else in this directory.
add_test(NAME headless_boot COMMAND Headless)
set_tests_properties(headless_boot PROPERTIES
    TIMEOUT 120
    # Asserted rather than just "exit 0": the engine is happy to keep going
    # after a subsystem fails, so a clean exit alone would not notice.
    PASS_REGULAR_EXPRESSION "System interface created"
    FAIL_REGULAR_EXPRESSION "FAILED: CreateSystemInterface"
)

# ------------------------------------------------------------------------------
# Browser tests.
#
# These need a real browser and so are built only for wasm. They are driven by
# tests/web/run_browser_tests.py, which serves the build over HTTP -- wasm
# cannot be loaded from a file:// URL -- and drives headless Chromium through
# Playwright.
# ------------------------------------------------------------------------------
if(EMSCRIPTEN)
    # The WebGL2 context the renderer creates. Links GLESContext.cpp itself
    # rather than a copy, so this covers the code that actually ships.
    add_executable(test_glescontext
        web/test_glescontext.cpp
        "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES/GLESContext.cpp")
    target_include_directories(test_glescontext PRIVATE
        "${PROJECT_SOURCE_DIR}/RenderDll/XRenderGLES")
    # A canvas is required, so this is an HTML target rather than bare JS.
    set_target_properties(test_glescontext PROPERTIES SUFFIX ".html")
    target_link_options(test_glescontext PRIVATE
        "SHELL:-s MIN_WEBGL_VERSION=2"
        "SHELL:-s MAX_WEBGL_VERSION=2"
        "SHELL:-s OFFSCREENCANVAS_SUPPORT=0")

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME glescontext_browser
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_CURRENT_SOURCE_DIR}/web/run_browser_tests.py"
                         "$<TARGET_FILE:test_glescontext>")
        set_tests_properties(glescontext_browser PROPERTIES TIMEOUT 300)
    endif()

    # The browser host: does the engine's frame loop actually run and keep
    # yielding to the page. See run_browser_tests.py --frames for why that is
    # the thing worth asserting.
    if(Python3_Interpreter_FOUND AND TARGET CryWeb)
        add_test(NAME web_host_frames
                 COMMAND ${Python3_EXECUTABLE}
                         "${CMAKE_CURRENT_SOURCE_DIR}/web/run_browser_tests.py"
                         "$<TARGET_FILE:CryWeb>" --frames 30)
        set_tests_properties(web_host_frames PROPERTIES TIMEOUT 300)
    endif()
endif()
