# coding: utf-8
# =============================================================================
# tests/migrate/CMakeLists.txt
#
# FernetDecoder 单元测试。直接将 FernetDecoder.cpp 与 Base64.cpp 加入测试目标
# 源文件列表，避免在 src/migrate/ 抽出额外静态库（参考 task 描述中的"或简单点：
# 直接在 test 中 target_sources 加入 FernetDecoder.cpp + Base64.cpp"）。
# =============================================================================

add_executable(test_fernet_decoder test_fernet_decoder.cpp)

# 直接编译 migrate 模块的源文件（不修改 src/migrate/CMakeLists.txt）
target_sources(test_fernet_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src/migrate/FernetDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/migrate/Base64.cpp
)

# migrate 头文件所在目录
# 同时显式将 src/sdk/ 加入 include path：FernetDecoder.h 中使用
# `#include "core/Result.h"` 形式，需要 src/sdk/ 在搜索路径中才能解析到
# src/sdk/core/Result.h。
target_include_directories(test_fernet_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src/migrate
    ${CMAKE_SOURCE_DIR}/src/sdk
)

target_link_libraries(test_fernet_decoder
    PRIVATE
        PwdVault::SdkCore      # core/Result.h、core/Error.h
        PwdVault::Warnings
        OpenSSL::Crypto        # HMAC、AES-128-CBC
        unofficial-sodium::sodium  # sodium_init / sodium_memzero
        GTest::gtest_main
)

target_compile_features(test_fernet_decoder PRIVATE cxx_std_20)

# 集成到 CTest
add_test(NAME test_fernet_decoder COMMAND test_fernet_decoder)

message(STATUS "PwdVault: tests/migrate/ 配置完成（test_fernet_decoder）")
