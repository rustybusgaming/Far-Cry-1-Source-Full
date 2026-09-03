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
