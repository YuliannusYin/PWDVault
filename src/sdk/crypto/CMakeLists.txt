# coding: utf-8
# =============================================================================
# src/sdk/crypto/CMakeLists.txt
#
# PwdVault 加密引擎模块。定义 pwdvault-crypto STATIC 库，提供：
#   - CryptoEngine.h / CryptoEngine.cpp
#
# 依赖：
#   PUBLIC  : pwdvault-sdk-core（头文件：ICryptoEngine/Types/Error/Result）
#   PRIVATE : OpenSSL::Crypto（AES-256-GCM via EVP）
#             unofficial-sodium::sodium（Argon2id + memzero + randombytes）
# =============================================================================

add_library(pwdvault-crypto STATIC
    CryptoEngine.cpp
)
add_library(PwdVault::Crypto ALIAS pwdvault-crypto)

target_include_directories(pwdvault-crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pwdvault-crypto
    PUBLIC
        pwdvault-sdk-core           # 头文件依赖（ICryptoEngine、Types、Error、Result）
        PwdVault::Warnings
    PRIVATE
        OpenSSL::Crypto
        unofficial-sodium::sodium   # vcpkg 中 libsodium 的 target 名
)

# 强制 C++20（与 core 一致，Types.h 使用了 std::span）
target_compile_features(pwdvault-crypto PUBLIC cxx_std_20)

message(STATUS "PwdVault: src/sdk/crypto/ 配置完成（STATIC 库）")
