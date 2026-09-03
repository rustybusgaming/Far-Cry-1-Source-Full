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
