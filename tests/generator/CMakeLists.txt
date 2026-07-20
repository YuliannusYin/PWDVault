# coding: utf-8
# =============================================================================
# tests/generator/CMakeLists.txt
#
# PasswordGenerator 单元测试目标。
# =============================================================================

add_executable(test_password_generator
    test_password_generator.cpp
)

target_link_libraries(test_password_generator
    PRIVATE
        PwdVault::Generator
        GTest::gtest_main
)

# 集成到 ctest
include(GTest)
gtest_discover_tests(test_password_generator)
