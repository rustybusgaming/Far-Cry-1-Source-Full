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
